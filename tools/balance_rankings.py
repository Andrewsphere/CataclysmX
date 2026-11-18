"""Rank current combat content against fixed reference panels; keep results under out/."""

import argparse
import csv
import hashlib
import html
import io
import json
import random
import statistics
import subprocess
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REFERENCES = ('zombie_shopper', 'cellar_zombie', 'zombie_police')
DEFAULTS = dict(scenario='duel', enemy='zombie_shopper', loadout='hammer', skill=3,
                outfit='standard', style='basic_balanced', art_xp=0, policy='attack', max_ticks=30000)
GROUPS = {
    'enemies': 'Enemy threat: player death % against hammer skill 0, hammer skill 3, two-hand sword skill 3',
    'weapons': 'Weapon loadouts: win % at skill 3, Balanced, standard outfit',
    'armor_duel': 'Armor: win % with two-hand sword, skill 3, Balanced',
    'armor_passive': 'Armor exposure: mean HP lost over 1000 ticks, unarmed skill 3 (lower is better)',
    'styles_unarmed': 'Unarmed styles: win % at skill 3, standard outfit',
    'styles_swordshield': 'Sword-and-shield styles: win % at skill 3, standard outfit',
    'progression_hammer': 'Hammer progression: win % by combat skill, Balanced, standard outfit',
    'progression_twohand': 'Two-hand sword progression: win % by combat skill, Balanced, standard outfit',
    'policy_duel': 'Novice hammer vs shopper: win % by policy (escapes reported separately)',
    'policy_crowd': 'Sword and shield vs three shoppers: win % by policy, skill 3',
    'policy_attrition': 'Sword and shield vs five sequential shoppers: win % by policy, skill 3',
}


def cases(catalog):
    """Explicit reference panels; each candidate in a group gets identical conditions."""
    for entry in catalog:
        if entry['kind'] == 'enemy':
            for loadout, skill in (('hammer', 0), ('hammer', 3), ('twohand', 3)):
                yield 'enemies', entry['id'], dict(enemy=entry['id'], loadout=loadout, skill=skill)
        elif entry['kind'] == 'weapon':
            for enemy in REFERENCES:
                label = f"{entry['id']} / {entry['grip']}" + (' [obsolete]' if entry['obsolete'] == '1' else '')
                yield 'weapons', label, dict(
                    enemy=enemy, weapon=entry['id'], grip=entry['grip'])

    for loadout in ('unarmed', 'swordshield', 'dual'):
        for enemy in REFERENCES:
            yield 'weapons', loadout, dict(enemy=enemy, loadout=loadout)

    outfits = [('none', dict(outfit='none')), ('standard outfit', dict(outfit='standard')),
               ('work outfit', dict(outfit='work'))]
    outfits += [(e['id'] + (' [obsolete]' if e['obsolete'] == '1' else ''), dict(outfit='none', wear=[e['id']]))
                for e in catalog if e['kind'] == 'armor']
    for candidate, outfit in outfits:
        for enemy in REFERENCES:
            yield 'armor_duel', candidate, dict(enemy=enemy, loadout='twohand') | outfit
            yield 'armor_passive', candidate, dict(enemy=enemy, loadout='unarmed',
                                                  scenario='passive', max_ticks=1000) | outfit

    # Learned styles need a compatible apparatus; comparing Boxing with a sword
    # against Weapon Guard unarmed would rank equipment availability, not styles.
    for loadout, art in (('unarmed', 'boxing'), ('swordshield', 'weapon_forms')):
        for entry in catalog:
            if entry['kind'] != 'style' or entry['art'] not in ('', art):
                continue
            for xp in ((0, 50000) if entry['art'] else (0,)):
                label = entry['id'] + (f' / XP {xp}' if entry['art'] else '')
                for enemy in REFERENCES:
                    yield f'styles_{loadout}', label, dict(enemy=enemy, loadout=loadout,
                                                          style=entry['id'], art_xp=xp)
    for loadout in ('hammer', 'twohand'):
        for skill in (0, 3, 6, 10):
            for enemy in REFERENCES:
                yield f'progression_{loadout}', f'skill {skill}', dict(enemy=enemy, loadout=loadout, skill=skill)
    for scenario in ('duel', 'crowd', 'attrition'):
        for policy in ('attack', 'cautious'):
            yield f'policy_{scenario}', policy, dict(scenario=scenario, policy=policy,
                loadout='hammer' if scenario == 'duel' else 'swordshield', skill=0 if scenario == 'duel' else 3)


def execute(command):
    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if result.returncode:
        raise RuntimeError(f"{subprocess.list2cmdline(command)}\n{result.stderr}")
    return result


def run_cell(job):
    index, setup, args = job
    raw = args.output / f'fight-{index:04d}.csv'
    command = [str(args.runner), '--seed', str(args.seed), '--seeds', str(args.seeds),
               '--csv', str(raw), '--csv-format', 'plain']
    for key, value in setup.items():
        for item in value if isinstance(value, list) else [value]:
            command.extend(('--' + key.replace('_', '-'), str(item)))
    result = execute(command)
    raw.with_suffix('.moves.txt').write_text(result.stderr, encoding='utf-8')
    summary, = csv.DictReader(io.StringIO(result.stdout))
    with raw.open(encoding='utf-8', newline='') as stream:
        runs = list(csv.DictReader(stream))
    if len(runs) != args.seeds:
        raise RuntimeError(f'Incomplete experiment: {raw}')
    return dict(command=command, setup=setup, raw=raw.name, summary=summary, runs=runs)


def interval(values):
    """Resample seeds as clusters: references sharing a seed are not independent."""
    rng = random.Random(0)
    samples = sorted(statistics.fmean(rng.choices(values, k=len(values))) for _ in range(1000))
    return [round(samples[24], 2), round(samples[974], 2)]


def rankings(entries, results):
    grouped = defaultdict(list)
    for group, candidate, key in entries:
        grouped[group, candidate].append(results[key])
    tables = defaultdict(list)
    for (group, candidate), cells in grouped.items():
        runs = [run for cell in cells for run in cell['runs']]
        counts = {outcome: sum(r['outcome'] == outcome for r in runs)
                  for outcome in ('win', 'loss', 'escape', 'timeout')}
        seed_scores = defaultdict(list)
        for run in runs:
            if group == 'armor_passive':
                score = int(run['in_hp_loss_100']) / 100
            else:
                score = 100 * (run['outcome'] == ('loss' if group == 'enemies' else 'win'))
            seed_scores[int(run['seed'])].append(score)
        wins = [r for r in runs if r['outcome'] == 'win']
        row = dict(candidate=candidate, score=round(statistics.fmean(
            statistics.fmean(v) for v in seed_scores.values()), 2),
            seed_interval95=interval([statistics.fmean(v) for v in seed_scores.values()]),
            fights=len(runs), cells=len(cells), **counts,
            mean_hp_lost=round(statistics.fmean(int(r['in_hp_loss_100']) / 100 for r in runs), 2),
            mean_armor_absorbed=round(statistics.fmean(int(r['in_armor_absorbed_100']) / 100 for r in runs), 2),
            win_hp_lost_median=statistics.median(int(r['in_hp_loss_100']) / 100 for r in wins) if wins else None,
            win_ticks_median=statistics.median(int(r['ticks']) for r in wins) if wins else None)
        tables[group].append(row)
    for group, rows in tables.items():
        rows.sort(key=lambda row: (row['score'] if group == 'armor_passive' else -row['score'], row['candidate']))
        rank = 0
        for index, row in enumerate(rows, 1):
            if index == 1 or row['score'] != rows[index - 2]['score']:
                rank = index
            row['rank'] = rank
    return tables


def write_csv(path, rows):
    with path.open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def report(path, tables, manifest):
    pieces = ['<!doctype html><meta charset="utf-8"><title>Combat power rankings</title>',
        '<style>body{font:15px system-ui;margin:2em;color:#222}table{border-collapse:collapse;margin-bottom:2em}',
        'th,td{padding:6px 10px;border:1px solid #ccc;text-align:right}th{background:#eee}',
        'td:nth-child(2){text-align:left}h2{margin-top:2em}</style>', '<h1>Combat power rankings</h1>',
        f"<p>{manifest['fights']} fights, {manifest['cells']} unique matchups; seeds "
        f"{manifest['seed']}–{manifest['seed'] + manifest['seeds'] - 1} per matchup.</p>",
        f"<p>Enemy AI: <strong>{html.escape(manifest['enemy_ai'])}</strong>. Recipe uses the normal monster controller; "
        'melee attacks in reach and otherwise wanders, which distorts knockback and escape comparisons.</p>',
        '<p>Scores are reference-panel results, not universal power. Each reference has equal weight. '
        'Enemies use player death percentage; other combat tables use player victory percentage. '
        'Escapes and timeouts are separate outcomes. Passive armor sorts by HP lost, lower first.</p>',
        '<p>Intervals: descriptive 95% seed-cluster bootstrap, fixed reference panel. Overlapping intervals '
        'do not establish an ordering; all-zero/all-one samples cannot establish certainty. '
        'Exact score ties share a rank. HP lost on wins excludes deaths; ticks on wins excludes timeouts. '
        'Passive timeouts normally mean surviving the full exposure.</p>',
        '<p>Adjacent starts, open arena, fixed automated policies, all combat skills set together. '
        'No ability use, healing, loot costs, durability, weather or exploration value. '
        'Armor garments are worn individually unless labeled as an outfit. Weapons include only authored '
        'melee profiles and named comparison loadouts; obsolete definitions are labeled, and firearms are measured as melee weapons. '
        'Styles use compatible reference loadouts; new arts need explicit panel assignments.</p>',
        '<p><a href="matrix.csv">Matchup matrix</a> · <a href="rankings.json">Ranking data</a> · '
        '<a href="manifest.json">Commands and provenance</a></p>']
    columns = ('rank', 'candidate', 'score', 'seed_interval95', 'fights', 'win', 'loss', 'escape', 'timeout',
               'mean_hp_lost', 'mean_armor_absorbed', 'win_hp_lost_median', 'win_ticks_median')
    for group, title in GROUPS.items():
        if group not in tables:
            continue
        pieces.append(f'<h2>{html.escape(title)}</h2><table><tr>')
        pieces.extend(f'<th>{html.escape(key.replace("_", " "))}</th>' for key in columns)
        pieces.append('</tr>')
        for row in tables[group]:
            pieces.append('<tr>')
            pieces.extend(f'<td>{html.escape(str(row[key]) if row[key] is not None else "—")}</td>' for key in columns)
            pieces.append('</tr>')
        pieces.append('</table>')
    path.write_text(''.join(pieces), encoding='utf-8')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_digest():
    hasher = hashlib.sha256()
    for folder in ('src', 'data'):
        for path in sorted((ROOT / folder).rglob('*')):
            if path.is_file():
                hasher.update(path.relative_to(ROOT).as_posix().encode())
                hasher.update(bytes.fromhex(digest(path)))
    return hasher.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runner', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'out' / 'balance')
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--seeds', type=int, default=96)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--enemy-ai', choices=('recipe', 'melee'), default='recipe')
    parser.add_argument('--groups', nargs='+', choices=list(GROUPS), default=list(GROUPS))
    args = parser.parse_args()
    args.runner = args.runner.resolve()
    args.output = args.output.resolve()
    if not args.output.is_relative_to(ROOT / 'out'):
        parser.error('Generated reports must be under the repository out/ directory')
    if args.seeds < 1 or args.seed < 0 or args.seed + args.seeds > 2**32 or args.jobs < 1:
        parser.error('Use positive seeds/jobs and a seed range within uint32')
    if args.output.exists() and any(args.output.iterdir()):
        parser.error('Output directory must be empty; choose a new directory to preserve previous runs')
    catalog = list(csv.DictReader(io.StringIO(execute([str(args.runner), '--list-catalog']).stdout)))
    args.output.mkdir(parents=True, exist_ok=True)
    provenance = dict(runner_sha256=digest(args.runner), source_data_sha256=source_digest(),
                      script_sha256=digest(Path(__file__)))
    entries, setups = [], {}
    for group, candidate, options in cases(catalog):
        if group not in args.groups:
            continue
        setup = DEFAULTS | options | dict(enemy_ai=args.enemy_ai)
        if 'weapon' in setup:
            del setup['loadout']
        key = json.dumps(setup, sort_keys=True)
        entries.append((group, candidate, key))
        setups[key] = setup
    print(f'Running {len(setups)} matchups x {args.seeds} seeds ({len(setups) * args.seeds} fights)', flush=True)
    jobs = [(index, setup, args) for index, setup in enumerate(setups.values(), 1)]
    results = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for key, result in zip(setups, pool.map(run_cell, jobs)):
            results[key] = result
            if len(results) % 20 == 0 or len(results) == len(setups):
                print(f'{len(results)}/{len(setups)} matchups complete', flush=True)
    if digest(args.runner) != provenance['runner_sha256'] or source_digest() != provenance['source_data_sha256']:
        raise RuntimeError('Engine/content changed during the run; raw results retained but rankings not published')
    tables = rankings(entries, results)
    matrix = [dict(group=group, candidate=candidate, raw=results[key]['raw'], **results[key]['summary'])
              for group, candidate, key in entries]
    write_csv(args.output / 'matrix.csv', matrix)
    manifest = dict(created_utc=datetime.now(timezone.utc).isoformat(), seed=args.seed, seeds=args.seeds,
                    cells=len(results), fights=len(results) * args.seeds, groups=args.groups,
                    references=REFERENCES, enemy_ai=args.enemy_ai, **provenance,
                    experiments=[{k: v for k, v in result.items() if k != 'runs'} for result in results.values()])
    for name, value in (('rankings', tables), ('manifest', manifest), ('catalog', catalog)):
        (args.output / f'{name}.json').write_text(json.dumps(value, indent=2), encoding='utf-8')
    report(args.output / 'index.html', tables, manifest)
    print(f"Report: {args.output / 'index.html'}")


if __name__ == '__main__':
    main()
