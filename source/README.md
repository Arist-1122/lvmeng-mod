# 可重编源码

本目录包含管理器、三个托管运行入口、原生图形分派、Bootstrap、ReShade SDK 头与许可证。最终 shader 与 BC7 改色数据作为已审查的构建输入随 Mod 运行包保存；本构建不重新推断角色或修改原游戏资源。

`evidence/` 保留来源验证回执和完整 Groups 元数据以便复核。归档元数据中的旧绝对路径仅表示历史来源，运行器不读取这些归档路径。发布运行合同与实际加载路径均使用相对定位或用户选择的位置。

Windows x64 需要 .NET Framework 4.x 的 `csc.exe`、Python 3、MSVC 和 Windows SDK。用户直接使用管理器 EXE 时不需要这些编译工具。

```text
python build_manager.py
python build_runtime.py --runtime ../runtime --toolchain <portable-core-build目录>
python stage_manager.py
```

也可通过 `LVMENG_NATIVE_TOOLCHAIN` 指定工具链根目录。`build_runtime.py` 只使用本目录中的项目源码和 SDK 头，重新编译 actor、Bootstrap、三个托管入口，执行生产受光 inl 的 API 垫片分派测试，再刷新合同与固定文件 SHA-256。它不启动游戏或调用加载器。

管理器的本机设置在 `manager/user-data/settings.json`，游戏位置由管理器写入所选 Mod 的 `runtime/game-location.json`。它们不属于不可变发布文件。`stage_manager.py` 只复制当前运行包清单内已校验的文件，不把本机游戏位置和控制日志带入 Mod。

原生角色受光先通过确切 VS/PS、纹理 mip0 归属、描述符与 root 状态双重快照核对，再选角色专属 Light 候选；未接受的绘制交给保留的旧战斗分派。这里没有给其他角色制造通用适配规则。每个新角色/皮肤 Mod 需要提供自己经过验证的运行包和清单。

运行包只包含改色后的纹理流；完整原版 stock 纹理流不分发。生产 Matcher 以稀疏块快速排除，再对真实上传内容计算完整 mip SHA-256 证明身份；稀疏匹配本身不授权替换。独立真实 core/WARP 夹具已确认72 mip与全部6个owner，最终游戏场景仍需实测。

`PortableRuntime.hpp` 从 addon 模块自身定位运行包，严格读取 GameLocation 配置。AddonInit 验证精确宿主、当前 core 实际路径、core 和合同 SHA，并保留会话/所有者/租约守卫。编译后的完整性检查和目录迁移检查不能代替实机画面验收。
