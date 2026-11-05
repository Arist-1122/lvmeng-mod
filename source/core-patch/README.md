# 自改 ReShade 核心的精确来源

此目录对应发布包中的 `core/dxgi.dll`，它不是未修改的官方发行二进制。

- 上游：`https://github.com/crosire/reshade.git`
- 精确 commit：`18deaa52de0c425a78b329e9cb3c497281cd00ec`
- 发布二进制 SHA-256：`1c82a60c70735dc3f1938a4761d29e0012897acd72f6f054d2a53d666b9c6665`
- 完整改动：`lvmeng-reshade-core.patch`，包含24个修改/新增文件，也包含原工作树中两个未跟踪的 BC7 源文件。
- ReShade 许可：`LICENSE-ReShade.md`；构建中各依赖的原始许可在 `licenses/`。
- 十一个上游子模块的 URL 与精确 gitlink commit 在 `submodules.json`。

`provenance.json` 将这些源码与原二进制构建回执关联：当前 tracked diff 和 header/inl 树的摘要均与产生该二进制的原构建记录一致。导出时已从精确上游 commit 生成独立干净归档，实际应用完整补丁，并逐文件比较所有24个修改文件，统一换行后与 owning source 一致。

## 取得与应用源码

```powershell
git clone https://github.com/crosire/reshade.git reshade-lvmeng
git -C reshade-lvmeng checkout --detach 18deaa52de0c425a78b329e9cb3c497281cd00ec
git -C reshade-lvmeng submodule update --init --recursive
git -C reshade-lvmeng apply --check <本目录绝对路径>\lvmeng-reshade-core.patch
git -C reshade-lvmeng apply <本目录绝对路径>\lvmeng-reshade-core.patch
```

原版项目可依其 README 在 Visual Studio 2017 或更新版本打开 solution，选择64位 Release 构建。下列脚本另外保留了发布二进制所用的显式 cl/link 构建方法，已把旧工作站路径改成参数；导出时只验证了脚本语法，没有再次进行完整151单元核心编译。

## 与原构建一致的工具链布局

原构建使用 MSVC 14.44.35207（cl 19.44.35229）、Windows SDK 10.0.26100.0 的工具/头与库，以及 Python 3。原工具链目录布局：

```text
<toolchain>/msvc/VC/Tools/MSVC/14.44.35207/
<toolchain>/sdk/microsoft.windows.sdk.cpp/c/
<toolchain>/sdk/microsoft.windows.sdk.cpp.x64/c/
<toolchain>/pydeps/       # Jinja2 3.1.6 / MarkupSafe 3.0.3，或使用已安装的对应Python依赖
```

编译器、SDK 和 Python 依赖不随 Mod 分发，可从 Microsoft / PyPI 的官方渠道准备；本源码包不安装它们。

```powershell
python prepare_generated.py --source <patched-source> --toolchain <toolchain>
python build_core_portable.py --source <patched-source> --toolchain <toolchain> --work <独立构建输出目录> --output head_bc7_upload_v1
```

生成器使用该 commit 自带的 Khronos XML 与固定 GLAD 平台头输入；`build_core_portable.py` 从项目元数据提取151单元的编译/链接参数，最多两个低优先级编译任务，并保留原6 GiB job memory上限。源码、编译结果和生成日志都在指定目录，不运行游戏或加载器。

这些步骤提供可取得的精确源码与构建方法，不承诺在不同路径、工具链或时间得到逐字节相同的 DLL。新编译核心应作为新的开发构建独立复核，并同步运行合同、托管加载器固定 SHA 和 native 固定 SHA，再刷新发布清单；不要把不同 SHA 的核心直接塞进已封好的 Mod。
