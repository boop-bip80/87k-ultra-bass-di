# Experimental J201-like NJF primitive (opt-in only)

The new `src/dsp/JfetDevicePhysics.h` is an isolated DC device primitive;
no existing plugin DSP includes it. The production path and its legacy output
remain byte-for-byte unchanged. To adopt this model, a separate stage-level
integration and validation are required; its existence is **not** evidence of
an accurate retail J201 pedal or hardware clone.

The NJF channel equation uses the ngspice LEVEL-1 formulation with `B=1`,
`Vgst=Vgs−VTO`, and `betaPrime=BETA(1+LAMBDA*Vds)`, for forward `Vds>=0`:

- Off (`Vgst<=0`): `Id=GMIN*Vds`.
- Triode (`0<=Vds<=Vgst`): `Id=betaPrime*Vds*(2*Vgst−Vds)+GMIN*Vds`.
- Saturation (`Vds>Vgst`): `Id=betaPrime*Vgst^2+GMIN*Vds`.

Negative `Vds` swaps intrinsic drain/source reference and reverses current,
instead of extrapolating the triode polynomial into negative voltage. A
separate Shockley DC junction helper allows evaluating gate-to-source and
gate-to-drain forward conduction; it is **not** part of the resistor fixture's
ideal gate bias circuit. The helper is not a complete gate network/limiter and
can overflow for extreme, unrealistic applied forward junction voltage.

`solveNineVoltResistorLoad` handles only the explicit topology:
9V-or-less supply → `Rd` → drain, source → `Rs` → ground, ideal gate bias
`<=0V`. It brackets the root in `[0,Vdd/(Rd+Rs)]`; drain/source voltages are
limited by Kirchhoff's laws and the supply, not by clipping an already solved
node. It rejects positive gate bias, supply above 9 V, invalid resistances,
and invalid model parameters. The test checks resistor currents and channel
current independently, plus branch boundaries, polarity, diode signs, and
out-of-range rejection.

This fixture is **not** an integrated replacement for the pedal's coupled
Q1/Q2 stage: Q2's dynamic drain load, bias network, parasitic capacitances,
charge storage, breakdown, the complete forward-gate circuit, and transient
SPICE convergence are not modeled. The default beta and threshold are merely
plausible numerical examples, not identified physical part measurements.
J201 manufacturer specifications span broad device ranges; simulation
requires measured or fitted parameters for the actual transistor and circuit.

Sources:

- [ngspice manual, LEVEL-1 NJF/PJF with Parker–Skellern modification](https://nmg.gitlab.io/ngspice-manual/jfets/jfetmodels_njf_pjf/jfetlevel1modelwithparkerskellernmodification.html)
- [Central Semiconductor J201 data sheet](https://my.centralsemi.com/datasheets/J201.PDF)

Standalone test (no JUCE build required):

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic tests/JfetDevicePhysicsTest.cpp -o /tmp/jfet-device-physics-test
/tmp/jfet-device-physics-test
```
