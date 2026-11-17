# Critic gate (CRITIC_BRIEF.md, iteration 3 section 5).
#   python Tools/critic_round.py select <round> <run_dir>   -> Saved/Review/IT3/critic/round<N>/frame_01..30.jpg
#   python Tools/critic_round.py gate <round>               -> appends the verbatim answer and the computed result to CRITIC_LOG.md
# Sampling: 10 key moments chosen from the run's shots_index.json (state recorded with every shot) and 20 random
# frames from the rest of the run, a different random draw each round (seeded by the round number). Frame names are
# neutral; which frame is which is kept in selection.json one folder up, where the critic does not look.
import json, os, random, shutil, sys, datetime
from collections import Counter
from PIL import Image
import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
CRIT = os.path.join(ROOT, 'Saved', 'Review', 'IT3', 'critic')
PASS = {'AA', 'AA+', 'AAA'}
TIERS = ['indie', 'AA-', 'AA', 'AA+', 'AAA']
KEYS = ['cutscene', 'handover', 'jetpack_fight', 'debris_grab', 'reload', 'tandem_hook', 'canopy_opening', 'landing', 'airship_view', 'cloud_pass']


def mean_lum(path):
    a = np.asarray(Image.open(path).convert('L').resize((160, 90)), dtype=np.float32) / 255.0
    return float(a.mean())


def pick_keys(idx, run):
    shots = [s for s in idx if os.path.exists(os.path.join(run, s['file'])) and mean_lum(os.path.join(run, s['file'])) > 0.04]
    intro = [s for s in shots if s['intro']]
    play = [s for s in shots if not s['intro']]
    end_intro = intro[-1]['t'] if intro else (play[0]['t'] if play else 0)
    def first(cands, key=None):
        cands = [c for c in cands if c]
        if key: cands.sort(key=key)
        return cands[0] if cands else None
    mid = lambda L: L[len(L) // 2] if L else None
    k = {}
    k['cutscene'] = intro[int(len(intro) * 0.55)] if intro else None
    k['handover'] = first([s for s in play if 0.3 <= s['t'] - end_intro <= 2.0])
    k['jetpack_fight'] = first([s for s in play if s['jetpack_on_screen_dist'] > 0], key=lambda s: s['jetpack_on_screen_dist'])
    k['debris_grab'] = mid([s for s in play if s['hold']])
    k['reload'] = mid([s for s in play if s['reload'] and not s['canopy']])
    k['tandem_hook'] = mid([s for s in play if s['tandem'] and not s['canopy']])
    k['canopy_opening'] = first([s for s in play if s['canopy'] and 0.7 <= s['canopy_clock'] <= 2.5])
    k['landing'] = first([s for s in play if s['canopy'] and s['agl_m'] < 60], key=lambda s: s['agl_m'])
    k['airship_view'] = first([s for s in play if s['airship_on_screen'] and s['t'] - end_intro < 40])
    k['cloud_pass'] = first([s for s in play if s['cloud_veil'] > 0.25], key=lambda s: -s['cloud_veil'])
    return k, shots


def select(rnd, run):
    idx = json.load(open(os.path.join(run, 'shots_index.json')))
    keys, shots = pick_keys(idx, run)
    used = {s['file'] for s in keys.values() if s}
    rng = random.Random(1000 + rnd)
    rest = [s for s in shots if s['file'] not in used and not s['intro']]
    need = 30 - len(used)
    rand = rng.sample(rest, min(need, len(rest)))
    items = [(name, s) for name, s in keys.items() if s] + [('random', s) for s in rand]
    rng.shuffle(items)
    out = os.path.join(CRIT, f'round{rnd:02d}')
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    sel = []
    for i, (name, s) in enumerate(items, 1):
        f = f'frame_{i:02d}.jpg'
        Image.open(os.path.join(run, s['file'])).convert('RGB').save(os.path.join(out, f), quality=92)
        sel.append({'frame': f, 'kind': name, 'source': s['file'], 't': s['t']})
    json.dump({'round': rnd, 'run': run, 'missing_keys': [k for k, v in keys.items() if not v], 'frames': sel}, open(os.path.join(CRIT, f'round{rnd:02d}_selection.json'), 'w'), indent=1)
    print('selected', len(sel), 'missing keys:', [k for k, v in keys.items() if not v])


def gate(rnd):
    out = os.path.join(CRIT, f'round{rnd:02d}')
    raw = open(os.path.join(out, 'answer.json'), encoding='utf-8').read()
    ans = json.loads(raw)
    sel = json.load(open(os.path.join(CRIT, f'round{rnd:02d}_selection.json')))
    kinds = {f['frame']: f['kind'] for f in sel['frames']}
    assert len(ans) == len(sel['frames']), f'{len(ans)} answers for {len(sel["frames"])} frames'
    counts = Counter(a['tier'] for a in ans)
    failing = [a['frame'] for a in ans if a['tier'] not in PASS]
    cats = Counter(i.split(':', 1)[0].strip().lower() for a in ans for i in a['issues'])
    passed = not failing
    log = os.path.join(ROOT, 'CRITIC_LOG.md')
    if not os.path.exists(log):
        open(log, 'w', encoding='utf-8').write('# CRITIC_LOG\n\nОтветы критика дословно, по раундам. Задание: CRITIC_BRIEF.md (не меняется). Решение «прошёл / не прошёл» считает `Tools/critic_round.py` из JSON: ворота пройдены, только если все 30 кадров `AA`, `AA+` или `AAA`.\n')
    with open(log, 'a', encoding='utf-8') as f:
        f.write(f'\n## Раунд {rnd} ({datetime.date.today().isoformat()})\n\n')
        f.write('Уровни: ' + ', '.join(f'{t} {counts.get(t, 0)}' for t in TIERS) + '\n\n')
        f.write(f'Итог: **{"пройдено" if passed else "не пройдено"}**' + ('' if passed else f' ({len(failing)} кадров ниже AA: ' + ', '.join(f'{x} ({kinds.get(x)})' for x in failing) + ')') + '\n\n')
        f.write('Частота категорий проблем: ' + ', '.join(f'{c} {n}' for c, n in cats.most_common()) + '\n\n')
        f.write('Ключевые кадры: ' + ', '.join(f'{f["frame"]} {f["kind"]}' for f in sel['frames'] if f['kind'] != 'random') + '\n\n')
        f.write('Ответ критика:\n\n```json\n' + raw.strip() + '\n```\n')
    print(json.dumps({'round': rnd, 'tiers': {t: counts.get(t, 0) for t in TIERS}, 'passed': passed, 'failing': len(failing), 'categories': cats.most_common()}, ensure_ascii=False))


if __name__ == '__main__':
    if sys.argv[1] == 'select':
        select(int(sys.argv[2]), sys.argv[3])
    else:
        gate(int(sys.argv[2]))
