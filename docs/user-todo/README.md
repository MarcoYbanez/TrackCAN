# Owner to-do (things only you can do)

Claude cannot touch the hardware, so this folder collects everything that needs your hands,
your scope or your knowledge of the car. Work from the checklist, tick items off, and tell
Claude the results so they go into `docs/journal/`.

- [hardware-redundancy.md](hardware-redundancy.md): every extra wire, tap, divider and part the
  redundancy layers need (readback divider, second sensor tap, voltage limits), with the
  Feather pin map. Read this one for "what do I have to add to the hardware".
- [hardware-todo.md](hardware-todo.md): bench measurements, wiring and protection, and facts
  only you know (ECU, sensor, tune). Items are ordered **safest first**: do the bench
  measurements before anything is connected to the ECU.

Rules for this folder:

- Part numbers and measurements only. No addresses, payment details or invoice scans
  (`assets/invoices/` is git-ignored for that reason).
- Every item says why it matters. If a result surprises you, stop and tell Claude before
  wiring to the car.
