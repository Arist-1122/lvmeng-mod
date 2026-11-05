"""Generate with fixed core registries and fixed glad platform headers; no network."""
from pathlib import Path
import sys,runpy,os
from urllib.parse import urlparse
S=Path(os.environ['RESHAPE_PINNED_SOURCE'])
sys.path.insert(0,str(S/'deps/glad'))
import glad.files
Base=glad.files.StaticFileOpener
class PinnedOpener(Base):
    def urlopen(self,url,data=None,*args,**kwargs):
        if data is not None:raise RuntimeError('Payload requests forbidden')
        name=urlparse(url).path.rsplit('/',1)[-1]
        if name in ('gl.xml','wgl.xml','vk.xml'):
            return (S/'deps/khronos'/name).open('rb')
        return super().urlopen(url,data,*args,**kwargs)
glad.files.StaticFileOpener=PinnedOpener
runpy.run_module('glad',run_name='__main__')
