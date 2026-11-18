"""Re-run the small design reference matchups against the built combat experiment."""

import argparse
import csv
import io
import subprocess
from pathlib import Path


def matchups():
    for enemy in ('zombie_shopper', 'cellar_zombie', 'zombie_police'):
        for skill in (0, 3):
            yield dict(enemy=enemy, loadout='hammer', skill=skill)
        yield dict(enemy=enemy, loadout='hammer', skill=0, policy='cautious')
    for loadout in ('unarmed', 'knife', 'sword', 'swordshield', 'twohand', 'axe'):
        yield dict(loadout=loadout)
    for outfit in ('none', 'standard', 'work'):
        for enemy in ('zombie_shopper', 'cellar_zombie'):
            yield dict(enemy=enemy, outfit=outfit, scenario='passive', loadout='unarmed', max_ticks=1000)
            if enemy != 'zombie_shopper' or outfit != 'standard':
                yield dict(enemy=enemy, outfit=outfit, loadout='twohand')
    for style, loadout in [('boxing_guard', 'unarmed'), ('weapon_guard', 'swordshield')]:
        for xp in (0, 50000):
            yield dict(loadout=loadout, style=style, art_xp=xp)
    for policy in ('attack', 'cautious'):
        yield dict(loadout='swordshield', scenario='crowd', policy=policy)


def write_workbook(path, columns, rows):
    import xlsxwriter  # Optional; the simulation and CSV export need only Python.
    with xlsxwriter.Workbook(path, {'strings_to_numbers': True, 'strings_to_formulas': False,
                                    'strings_to_urls': False}) as book:
        def sheet(name, header, values):
            ws = book.add_worksheet(name)
            ws.add_table(0, 0, len(values), len(header) - 1, {
                'columns': [{'header': col} for col in header], 'data': values})
            ws.freeze_panes(1, 1)
            ws.autofit(250)

        sheet('Current baseline', columns, [[row[key] for key in columns] for row in rows])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runner', type=Path)
    parser.add_argument('output', type=Path, nargs='?', default=Path('out/balance-baseline.csv'))
    parser.add_argument('--seeds', type=int, default=96)
    parser.add_argument('--enemy-ai', choices=('recipe', 'melee'), default='recipe')
    parser.add_argument('--workbook', type=Path, help='Also write an Excel workbook (requires XlsxWriter)')
    args = parser.parse_args()
    local = Path(__file__).resolve().parents[1] / 'out'
    for path in (args.output, args.workbook):
        if path is not None and not path.resolve().is_relative_to(local):
            parser.error('Generated reports must be under the repository out/ directory')
    if args.seeds < 1:
        parser.error('--seeds must be positive')
    defaults = dict(scenario='duel', enemy='zombie_shopper', loadout='hammer', skill=3,
                    outfit='standard', style='basic_balanced', art_xp=0, policy='attack', max_ticks=30000,
                    enemy_ai=args.enemy_ai)
    columns = ['enemy', 'enemy_ai', 'scenario', 'loadout', 'style', 'skill', 'outfit', 'policy', 'wins', 'losses', 'escapes', 'timeouts', 'ticks_median',
               'hp_dealt_median', 'hp_lost_median', 'hp_lost_on_wins_median', 'armor_absorbed_100_median',
               'retreat_steps_total', 'flee_steps_total', 'rests_total', 'art_xp', 'first_seed', 'runs', 'max_ticks']
    rows = []
    for options in matchups():
        setup = defaults | options
        cmd = [str(args.runner.resolve()), '--seed', '1', '--seeds', str(args.seeds)]
        for key, value in setup.items():
            cmd.extend(('--' + key.replace('_', '-'), str(value)))
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(f"{' '.join(cmd)}: {result.stderr}")
        row, = csv.DictReader(io.StringIO(result.stdout))
        for dest, src in [('hp_dealt_median', 'hp_dealt_100_median'), ('hp_lost_median', 'hp_lost_100_median'),
                          ('hp_lost_on_wins_median', 'win_hp_lost_100_median')]:
            row[dest] = int(row[src]) // 100 if row[src] else ''  # Injury is resolved in whole HP.
        rows.append(row)
    with args.output.open('w', encoding='utf-8-sig', newline='') as out:
        out.write('sep=;\r\n')
        writer = csv.DictWriter(out, columns, delimiter=';', extrasaction='ignore')
        writer.writeheader()
        writer.writerows(rows)
    if args.workbook:
        write_workbook(args.workbook, columns, rows)


if __name__ == '__main__':
    main()
