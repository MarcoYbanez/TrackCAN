---
name: automotive-safety-research
description: Verified-source research for an unknown safety behavior on a TrackCAN Node (sensor-fault output, fallback values, watchdogs, boot or reset behavior, fault debouncing). Use when a safety decision has no settled answer in docs/journal/.
---

Safety behavior in this repo comes from how modern automotive ECUs handle the same case, backed by a source you have read. A wrong ethanol value can damage the engine, so a plausible guess is not an answer.

## Steps

1. Search `docs/journal/` for the topic. A settled answer there is the answer: cite it and stop.
2. Research how modern automotive ECUs handle the case. Rank sources:
   - **Tier 1**: standards and specifications (AUTOSAR, ISO 26262, IEC 61508, SAE) and silicon datasheets and errata (RP2040, the DAC, the transceiver).
   - **Tier 2**: ECU and tuning-software documentation, semiconductor application notes, OEM and supplier white papers.
   - **Tier 3**: patents. They show a technique exists, not that it is standard practice.
   - **Leads only**: forums, blogs, vendor marketing. Use them to find a Tier 1 or 2 source, then cite that source.
3. Read the source text itself and quote the sentence with its section number. For PDFs, follow [pdf-method.md](pdf-method.md).
4. Record each claim in `docs/journal/` as a dated entry: the claim, a tag (`[verified]` read in a Tier 1-2 source this session, `[doc]` stated by a page not cross-checked, `[unverified]` not sourced), the source, and the section.
5. When no Tier 1-2 source exists, write "no verified source found", list what was searched, and put the decision to the user.

Done when every safety claim carries a source and section number or the tag `[unverified]`, and the journal entry exists.

## Report absences

Record what a source does not state as its own finding. Example: the AUTOSAR Dem specification has no "substitute value" text, and the DFR0971 page gives no power-on output. A stated absence tells the owner what to measure.
