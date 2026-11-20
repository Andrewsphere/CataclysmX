# New civic places

`data/worldgen/buildings/city_civic.json5` supplies newer civic patterns. They
are selection opportunities, not guaranteed coverage. The clinic is eligible in
all settlement sizes; the school and both service-hub variants start at medium.
Family weights are 850 for the clinic, 800 for the school, 850 for the shared
service-hub family, and 700 for the outdoor forecourt. The two hub variants
share the family weight rather than doubling its selection weight.

The clinic has one occupied floor: a glazed waiting/reception area, two
treatment and staff-store rooms, and a rear door. The two-floor school has a
ground-floor practical wing and library around a shared hall, classrooms upstairs,
and front and rear doors. Its one home polymer printer and one ground draw from
`loot_printer_filament_large` provide 100–200 filament portions, without a
quantity multiplier. The two-floor service hub has a public hall, maintenance
rooms and upstairs records. Its resident-services variant substitutes waiting
stations and terminals for the public hall and puts staff conveniences behind
the counter. Both have independent front and rear ground doors.

The forecourt is an outdoor square: open pale paving, seating and a small retail
connection surround a partial canopy supported by two concrete pillars. Its
ground is exterior space; the canopy is not an operable transport structure.
Terminals, checkpoints
and notice boards are passive, and the clinic does not add medical actions.
Roofs do not count as occupied floors.

## Authored population baseline

These are sums of marker probabilities, **before** the generator's interior
5×/6×/7× settlement-size adjustment, not guaranteed actor counts:

| Pattern | Occupied floors | Ground | Upper | Total | Roles |
| --- | ---: | ---: | ---: | ---: | --- |
| Neighborhood clinic | 1 | 1.30 | — | 1.30 | Treatment staff and waiting visitors |
| Polytechnic school | 2 | 1.50 | 1.50 | 3.00 | Practical wing, library/hall and classrooms |
| Municipal service hub | 2 | 1.00 | 1.00 | 2.00 | Maintenance, hall and records staff |
| Resident services office | 2 | 1.00 | 1.00 | 2.00 | Staff, waiting and upstairs offices |
| Transit forecourt | outdoor | 0.50 | — | 0.50 | Waiting shoppers; part of the outdoor budget, not the interior multiplier |

Existing loot pools supply medical, office, reading, electrical, tool, utility
and food stock. Equipment behavior and available actions remain those of the
existing objects and items.
