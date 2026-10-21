# ESP32 + CC1101 Perfboard Case

A press-fit 3D-printable case sized for a 70x30mm perfboard carrying an
ESP32 dev module and a CC1101 breakout, built for the `quiet_cool` ESPHome
component.

- `QuietCoolCase.FCStd` — parametric FreeCAD source
- `QuietCoolCase_Body.stl` / `QuietCoolCase_Body.step` — case body
- `QuietCoolCase_Lid.stl` / `QuietCoolCase_Lid.step` — press-fit lid

## Key dimensions

| | |
|---|---|
| Outer footprint | 74 x 47mm |
| Interior cavity | 71 x 44mm |
| Wall thickness | 1.5mm |
| Assembled height | 26.6mm |
| Board | 70 x 30 x 1.6mm, mounted on 4 heat-stake standoffs (7mm tall, 2mm inset from each board corner) 0.5mm off the left wall |
| USB cutout | Micro-USB, front wall, centered on board width, 7mm above the board |
| Antenna hole | 7mm dia, right side wall (facing the USB end), 11mm in from the back edge, 5mm above the board |
| Lid | Press-fit skirt, 1.5mm wall thickness, 0.15mm/side clearance, 12 vent holes (4x3 grid) |

## Printing notes

- Print the case body as modeled (floor down, open top up) — no supports needed.
- Print the lid **flipped** from how it's modeled (flat plate down on the bed,
  skirt facing up) for a clean first layer and zero overhangs.
- The two standoffs nearest the left wall sit ~0.25mm from the inner wall
  face; on a 0.4mm nozzle this will likely print fused to the wall, which is
  harmless (it just adds support there).
