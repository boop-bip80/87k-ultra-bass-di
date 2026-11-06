# Round 7 — physical-model acceptance gates (research, not retail parity)

**Scope:** the *Black Mirror VII v1.6* third-party clone's J201 Q1/Q2 and
ATTACK circuitry, plus independent physical bounds. This is a prospective
engineering gate. It is not a claim that an actual B7K Ultra PCB uses this
topology, supply distribution, transistor bias, or component set.

## Evidence hierarchy

1. **Manufacturer facts**: the [Darkglass B7K Ultra manual](https://www.darkglass.com/en-gb/pages/microtubes-b7k-ultra-manual)
   publishes nominal 1 MΩ input, 1 kΩ output and external 9 V DC supply.
   The manual does **not** disclose internal supply rails, PCB revisions, JFET
   part numbers, or detailed analog schematic. The external 9 V rating alone
   cannot prove that every internal circuit node stays below +9 V (internal
   converters are possible).
2. **Clone component evidence**: the [Black Mirror VII 1.6v build document](https://pcbguitarmania.com/wp-content/uploads/2022/07/Black-Mirror-VII-1.6v-Building-Docs.pdf)
   identifies the DIY circuit, *not* an authenticated manufacturer revision.
3. **Device equations**: the [ngspice Level-1 NJFET reference](https://nmg.gitlab.io/ngspice-manual/jfets/jfetmodels_njf_pjf/jfetlevel1modelwithparkerskellernmodification.html)
   specifies transistor current regions. With default Parker–Skellern B=1 and
   a forward-oriented drain, define
   $u=V_{GS}-V_{TO}$ and $\beta_p=\beta(1+\lambda V_{DS})$.
   Ignoring small GMIN current:

   - cutoff for $u\le0$: $I_D\approx0$
   - linear/triode for $0\le V_{DS}\le u$:
     $I_D=\beta_p V_{DS}(2u-V_{DS})$
   - saturation for $V_{DS}\ge u$:
     $I_D=\beta_p u^2$

   Reverse drain/source orientation, gate-junction conduction, charge
   storage, and real-device departures must be tested or explicitly excluded.

## Research-mode acceptance matrix

| Gate | Required check | Must not be inferred |
|---|---|---|
| Q1 DC bias | Independent ngspice DC operating-point comparison with stated initial conditions, rail references and part assumptions | retail J201 bias |
| Q1 piecewise current | Transition continuity at $V_{DS}=0,u$; cutoff, triode and saturation; reverse $V_{DS}$ orientation treated explicitly | a constant `r_o` is a real drain-region model |
| Q1/Q2 joint solve | KCL at **every** solved node; physical supply and transistor compliance inequalities for the specified **clone** fixture | DC-equivalent offset can be obtained by clipping incremental drain volts |
| Switched ATTACK | Each fade branch carries an internally consistent JFET source-history state paired with its own TrebleAttack network; no single state is advanced twice | crossfading two passive port voltages is itself a circuit solution |
| Numerical safety | Finite bounded output, bounded solver iterations, no new allocations or locks in the audio callback; test 8–384 kHz and abusive input ranges | finite output alone is physically accurate |
| Default compatibility | Research mode disabled by default; bit-exact full-chain output across a fixed 180,000-sample golden configuration grid | success on this gate implies better pedal tone |
| Independent oracle | Native ngspice run plus adversarial perturbation, held-out levels and gate/drain-source polarities | testing against another port of the same solver proves independence |

## Required physical data before claiming retail parity

- Exact retail model name, revision/serial scope and authenticated PCB
  schematic or trace with supply rails and bias components identified.
- Measured Q1/Q2 DC node voltages and, where practical, gate/drain transfer
  curves under controlled input voltage and source loading.
- Complex input impedance, 1/4-inch output response, both distortion and
  clean paths, ATTACK/GRUNT/selector positions, and DI/IR paths separately.
- Calibrated re-amped input and output WAV recordings (original unprocessed
  references), metadata, checksums, repeated units and held-out settings.
- Blind level-matched A/B tests and predefined error thresholds; do not fit
  and grade on the same recordings.

**Release policy:** A mathematical J201/Q2 prototype can pass the first
several gates while the actual B7K Ultra remains **unverified**. It may be
merged as a default-OFF research implementation with a plainly worded scope.
Enabling it in shipped presets requires additional evidence and audio tests.
