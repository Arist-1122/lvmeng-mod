"""Stage the real verified runtime as the manager's built-in Mod."""
from pathlib import Path
import datetime,json,sys
sys.dont_write_bytecode=True
from stage_mod import stage

SOURCE=Path(__file__).resolve().parent;ROOT=SOURCE.parent
mods=ROOT/'manager/mods';mods.mkdir(parents=True,exist_ok=True)
target=mods/'litelisi-redblack'
if target.exists():
    history=ROOT/'staging_history';history.mkdir(exist_ok=True)
    target.rename(history/('litelisi-redblack-'+datetime.datetime.now().strftime('%Y%m%dT%H%M%S%f')))
mod=stage(ROOT/'runtime',target,'litelisi-redblack','莉特莉丝 · 红黑与技能受光整合试用','role_26','role_26_02')
print(json.dumps(dict(mod=mod,fixedFiles=len(json.loads((target/'runtime/package.json').read_text(encoding='utf-8'))['files']),gameLocationIncluded=(target/'runtime/game-location.json').exists()),ensure_ascii=False))
