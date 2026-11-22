/*******************************************************
 * @file dash_sim.c
 * @brief A DEMO drive: see dash_sim.h. Integer only so it behaves the same on the M4 and on the
 * host.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "dash_sim.h"

/*******************************************************
 * Macros
 *******************************************************/
#define CRANK_MS 1000u
#define CHARGE_LAMP_OFF_MS 2500u /* the charge lamp goes out shortly after the engine starts */
#define IDLE_RPM 850
#define CRANK_RPM 250
#define REV_LIMIT 7500
#define SHIFT_MS 300u
#define DT_MAX_MS 100u
#define NEVER_UPSHIFT 65535u

/*******************************************************
 * Type Definitions
 *******************************************************/
typedef enum { PH_IDLE, PH_PULL, PH_CRUISE, PH_BRAKE } phase_kind_t;

typedef struct {
    phase_kind_t kind;
    uint16_t speed_kmh; /* PULL: end speed. CRUISE: speed to hold. */
    uint8_t throttle;   /* PULL only */
    uint8_t max_gear;
    uint16_t upshift_rpm;
    uint32_t dur_ms; /* IDLE and CRUISE only */
} phase_t;

/*******************************************************
 * Static Variables
 *******************************************************/
/* Engine rpm per km/h, times 100: 1000 / 60 / 1.94 m x (gear ratio x 3.9). */
static const int32_t k_rpm_per_kmh_x100[6] = {0, 11572, 6523, 4577, 3257, 2473};
/* Full-throttle acceleration in each gear, km/h per second x 100. */
static const int32_t k_accel_x100[6] = {0, 2000, 1700, 1300, 900, 600};

static const phase_t k_phases[] = {
    {PH_IDLE, 0, 0, 0, NEVER_UPSHIFT, 4000}, {PH_PULL, 170, 100, 5, 6800, 0},
    {PH_CRUISE, 110, 0, 5, 3500, 9000},      {PH_PULL, 140, 100, 3, 7000, 0},
    {PH_CRUISE, 80, 0, 5, 3500, 7000},       {PH_BRAKE, 0, 0, 5, NEVER_UPSHIFT, 0},
    {PH_IDLE, 0, 0, 0, NEVER_UPSHIFT, 6000},
};
#define PHASE_COUNT ((uint8_t)(sizeof(k_phases) / sizeof(k_phases[0])))
#define LAST_PHASE ((uint8_t)(PHASE_COUNT - 1u))

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int32_t div_round100(int32_t v)
{
    return (v + (v >= 0 ? 50 : -50)) / 100;
}

/* One step of a first order lag: *state_x100 moves toward target (plain units) with time constant
 * tau_ms. */
static void lag(int32_t *state_x100, int32_t target, uint32_t dt_ms, uint32_t tau_ms)
{
    *state_x100 += ((target * 100 - *state_x100) * (int32_t)dt_ms) / (int32_t)tau_ms;
}

/* Trapezoid 0..1000: rises over [a, b], holds to c, falls over [c, d]. Used for a fault that comes
 * and goes. */
static int32_t trapezoid(uint32_t t, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    if (t <= a || t >= d) {
        return 0;
    }
    if (t < b) {
        return (int32_t)((t - a) * 1000u / (b - a));
    }
    if (t <= c) {
        return 1000;
    }
    return (int32_t)((d - t) * 1000u / (d - c));
}

static int32_t full_throttle_boost_x10(int32_t rpm)
{
    if (rpm <= 1500) {
        return -20;
    }
    if (rpm <= 4000) {
        return -20 + (rpm - 1500) * 185 / 2500; /* spools up to 16.5 psi */
    }
    if (rpm <= 7000) {
        return 165 - (rpm - 4000) * 25 / 3000; /* tapers to 14.0 psi */
    }
    return 140;
}

static int32_t drag_x100(int32_t speed_x100)
{
    return 100 + speed_x100 * 17 / 1000; /* 2.7 km/h per second at 100 km/h */
}

static void select_gear(dash_sim_t *s, const phase_t *ph)
{
    int32_t speed_x100 = s->speed_x10000 / 100;
    int32_t rpm;

    if (ph->kind == PH_IDLE || (ph->kind == PH_BRAKE && speed_x100 <= 500)) {
        s->gear = 0;
        return;
    }
    if (s->gear == 0) {
        s->gear = 1; /* moving off */
    }
    if (s->gear > ph->max_gear) {
        s->gear = ph->max_gear; /* kick down */
        s->shift_ms = SHIFT_MS;
    }
    rpm = (int32_t)dash_sim_rpm_for(s->gear, (uint32_t)speed_x100);
    if (s->gear < ph->max_gear && rpm >= (int32_t)ph->upshift_rpm) {
        s->gear++;
        s->shift_ms = SHIFT_MS;
    } else if (s->gear > 1 && rpm < 2200) {
        s->gear--;
    }
}

static bool phase_done(const dash_sim_t *s, const phase_t *ph)
{
    switch (ph->kind) {
    case PH_IDLE:
    case PH_CRUISE:
        return s->phase_ms >= ph->dur_ms;
    case PH_PULL:
        return s->speed_x10000 / 100 >= (int32_t)ph->speed_kmh * 100;
    case PH_BRAKE:
        return s->speed_x10000 / 100 <= 200;
    }
    return true;
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
uint16_t dash_sim_rpm_for(uint8_t gear, uint32_t kmh_x100)
{
    if (gear < 1 || gear > 5) {
        return 0;
    }
    return (uint16_t)(((uint64_t)kmh_x100 * (uint64_t)k_rpm_per_kmh_x100[gear]) / 10000u);
}

void dash_sim_init(dash_sim_t *s)
{
    *s = (dash_sim_t){0};
    s->battery_x100 = 124 * 100; /* 12.4 V at rest, the starter will pull it down */
    s->afr_x100 = 147 * 100;
    s->coolant_x100 = 1800 * 100;
    s->oil_temp_x100 = 1700 * 100;
    s->fuel_micro_pct = 30 * 1000000;
}

void dash_sim_step(dash_sim_t *s, uint32_t dt_ms, dash_sim_out_t *out)
{
    const phase_t *ph;
    const bool fault_oil = (s->loop % 3u) == 1u;
    const bool fault_charge = (s->loop % 3u) == 2u;
    const bool final_idle = (s->phase == LAST_PHASE);
    bool cranking;
    int32_t speed_x100;
    int32_t t = 0; /* throttle, percent */
    int32_t acc;
    int32_t rpm_target;
    int32_t boost_target;
    int32_t afr_target;
    int32_t batt_target;
    int32_t press;
    int32_t rate;
    uint16_t lights = 0;

    if (dt_ms == 0) {
        dt_ms = 1;
    }
    if (dt_ms > DT_MAX_MS) {
        dt_ms = DT_MAX_MS;
    }
    s->uptime_ms += dt_ms;
    s->phase_ms += dt_ms;
    cranking = s->uptime_ms < CRANK_MS;
    ph = &k_phases[s->phase];

    select_gear(s, ph);
    speed_x100 = s->speed_x10000 / 100;

    /* Throttle and acceleration. */
    if (s->shift_ms > 0) {
        s->shift_ms = (s->shift_ms > dt_ms) ? s->shift_ms - dt_ms : 0; /* lifted during a shift */
    } else if (ph->kind == PH_PULL) {
        t = ph->throttle;
    } else if (ph->kind == PH_CRUISE) {
        int32_t base = drag_x100(speed_x100) * 100 / k_accel_x100[s->gear];
        int32_t err_kmh = ((int32_t)ph->speed_kmh * 100 - speed_x100) / 100;

        t = clamp32(base + err_kmh * 4, 0, 100);
    } else if (ph->kind == PH_IDLE && !cranking) {
        t = 3;
    }

    switch (ph->kind) {
    case PH_PULL:
        acc = (t > 0) ? k_accel_x100[s->gear] * t / 100 - drag_x100(speed_x100)
                      : -drag_x100(speed_x100) - 100;
        break;
    case PH_CRUISE:
        acc = clamp32(((int32_t)ph->speed_kmh * 100 - speed_x100) / 2, -700,
                      600); /* lifting and braking lightly */
        break;
    default: /* BRAKE and IDLE */
        acc = -1500;
        break;
    }
    s->speed_x10000 += acc * (int32_t)dt_ms / 10;
    if (s->speed_x10000 < 0) {
        s->speed_x10000 = 0;
    }
    speed_x100 = s->speed_x10000 / 100;

    /* RPM follows the road speed through the gear; in first gear the clutch slips at a launch. */
    if (cranking) {
        rpm_target = CRANK_RPM;
    } else if (s->gear == 0) {
        rpm_target = IDLE_RPM + (int32_t)((s->uptime_ms / 40u) % 25u) - 12; /* idle wobble */
    } else {
        rpm_target = (int32_t)dash_sim_rpm_for(s->gear, (uint32_t)speed_x100);
        if (s->gear == 1 && speed_x100 < 4000) {
            int32_t slip = IDLE_RPM + t * (4500 - IDLE_RPM) / 100;

            rpm_target = rpm_target > slip ? rpm_target : slip;
        }
    }
    rpm_target = clamp32(rpm_target, 0, REV_LIMIT);
    if (s->rpm < rpm_target) {
        rate = 9000 * (int32_t)dt_ms / 1000;
        s->rpm += (rpm_target - s->rpm) < rate ? (rpm_target - s->rpm) : rate;
    } else {
        rate = 6000 * (int32_t)dt_ms / 1000;
        s->rpm -= (s->rpm - rpm_target) < rate ? (s->rpm - rpm_target) : rate;
    }

    /* Boost: manifold vacuum at closed throttle, the boost curve at full throttle. */
    boost_target = cranking ? 0 : -120 + (full_throttle_boost_x10(s->rpm) + 120) * t / 100;
    lag(&s->boost_x100, boost_target, dt_ms, 400);

    /* AFR: closed loop 14.7, rich under boost, lean when the fuel is cut on lift off. */
    if (!cranking && t == 0 && s->rpm > 1800 && s->gear > 0) {
        afr_target = 220;
    } else if (div_round100(s->boost_x100) > 0) {
        int32_t b = clamp32(div_round100(s->boost_x100), 0, 100);

        afr_target = 147 - 32 * b / 100; /* 11.5 from 10 psi up */
    } else {
        afr_target = 147;
    }
    lag(&s->afr_x100, afr_target, dt_ms, 200);

    /* Temperatures drift toward a target that depends on the load. */
    lag(&s->coolant_x100, 1900 + t * 6, dt_ms, 18000);
    lag(&s->oil_temp_x100, 2050 + t * 6, dt_ms, 30000);

    /* Oil pressure rises with rpm and falls as the oil heats; an injected sag at the final idle of
     * loop 1. */
    press = 190 + (s->rpm - 850) * 835 / 10000 - (div_round100(s->oil_temp_x100) - 1800) * 3 / 100;
    if (s->rpm < 850) {
        press = press * s->rpm / 850;
    }
    if (cranking) {
        press = 0;
    }
    if (fault_oil && final_idle) {
        int32_t w = trapezoid(s->phase_ms, 1500, 3000, 5000, 6000);

        press = press + (10 - press) * w / 1000; /* sags to 1.0 psi, below the 2.1 psi switch */
    }
    lag(&s->oil_press_x100, press < 0 ? 0 : press, dt_ms, 150);

    /* Battery: the starter pulls it down, the alternator holds it up, a fault in loop 2 lets it
     * sag. */
    if (cranking) {
        batt_target = 105;
    } else if (fault_charge) {
        batt_target = 120;
    } else {
        batt_target = 139 + (s->rpm < 4000 ? s->rpm : 4000) * 5 / 4000;
    }
    lag(&s->battery_x100, batt_target, dt_ms, 300);

    /* Fuel drains faster under load; low fuel latches below 15 % and a refill happens at 10 %. */
    s->fuel_micro_pct -= (20000 + t * 5000) * (int32_t)dt_ms / 1000;
    if (s->fuel_micro_pct <= 10 * 1000000) {
        s->fuel_micro_pct = 90 * 1000000;
        s->fuel_low = false;
    }
    if (s->fuel_micro_pct < 15 * 1000000) {
        s->fuel_low = true;
    }

    /* Lamps. */
    if (div_round100(s->oil_press_x100) < 21) {
        lights |= DASH_SIM_LIGHT_OIL;
    }
    if (s->fuel_low) {
        lights |= DASH_SIM_LIGHT_FUEL_LOW;
    }
    if (s->uptime_ms < CHARGE_LAMP_OFF_MS || fault_charge) {
        lights |= DASH_SIM_LIGHT_CHARGE;
    }
    if (fault_charge && final_idle) {
        lights |= DASH_SIM_LIGHT_CHECK_ENGINE;
    }

    /* Move to the next phase when this one is finished. */
    if (phase_done(s, ph)) {
        s->phase_ms = 0;
        if (s->phase == LAST_PHASE) {
            s->phase = 1; /* the first idle (with the engine start) is not repeated */
            s->loop++;
        } else {
            s->phase++;
        }
    }

    out->rpm = (uint16_t)s->rpm;
    out->speed_kmh_x100 = (uint16_t)(s->speed_x10000 / 100);
    out->gear = s->gear;
    out->throttle_pct = (uint8_t)t;
    out->boost_psi_x10 = (int16_t)div_round100(s->boost_x100);
    out->afr_x10 = (uint16_t)div_round100(s->afr_x100);
    out->battery_volts_x10 = (uint16_t)div_round100(s->battery_x100);
    out->coolant_degf_x10 = div_round100(s->coolant_x100);
    out->oil_temp_degf_x10 = div_round100(s->oil_temp_x100);
    out->oil_pressure_psi_x10 = (uint16_t)div_round100(s->oil_press_x100);
    out->fuel_pct_x10 = (uint16_t)(s->fuel_micro_pct / 100000);
    out->lights = lights;
}
