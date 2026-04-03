# DicomViewerSkeleton

`DicomViewerSkeleton` 是一个基于 Qt、VTK、ITK 和 GDCM 的桌面医学影像查看工程，当前定位是一个可继续扩展的工作站式原型。

它已经具备以下核心能力：

- 加载 DICOM 序列目录
- 加载 NIfTI 影像文件，支持 `.nii` 和 `.nii.gz`
- 在同一个病例包中同时加载体数据和模型数据
- 以四窗格方式显示三视图 MPR 和 3D 模型视图
- 联动十字线位置
- 同步三视图窗宽窗位
- 在侧边栏中切换模型可见性
- 后台异步加载，并支持取消
- 兼顾中文路径场景

## 适用场景

这个工程适合以下用途：

- 作为医学影像查看器的基础骨架继续开发
- 验证 DICOM、NIfTI 和模型联动显示流程
- 在 Windows 桌面环境中快速构建一个可运行的医疗影像原型

## 当前支持的数据类型

### 影像

- DICOM 序列目录
- NIfTI 文件：`.nii`、`.nii.gz`

### 模型

- `.stl`
- `.obj`
- `.ply`
- `.vtp`
- `.vtk`

## 界面与交互

加载成功后，主界面会展示：

- 轴状位 MPR
- 冠状位 MPR
- 矢状位 MPR
- 3D 模型视图

当前界面支持的主要联动逻辑：

- 三个 MPR 视图共享体数据
- 十字线可在 MPR 与 3D 视图之间联动
- 窗宽窗位在三个 MPR 面板之间同步
- 模型可在侧边栏按对象开关显示

## 病例包目录约定

程序支持直接打开“病例包目录”。目录扫描规则大致如下：

- 优先识别 `dicom/` 或 `dicoms/` 中的 DICOM 序列
- 如果没有识别到 DICOM，则继续查找 NIfTI 文件
- 优先识别 `nifti/`、`nii/`、`image/`、`images/` 中的 `.nii/.nii.gz`
- 优先识别 `model/` 或 `models/` 中的模型文件
- 如果没有 `model/` 子目录，也会回退识别病例根目录下的模型文件

一个推荐的目录结构示例如下：

```text
case-root/
├─ dicom/
│  ├─ IM0001
│  ├─ IM0002
│  └─ ...
├─ model/
│  ├─ lesion.obj
│  └─ vessel.stl
└─ meta/
   └─ scene.json
```

NIfTI 病例也可以这样组织：

```text
case-root/
├─ nifti/
│  └─ lung.nii.gz
├─ model/
│  └─ surface.obj
└─ meta/
   └─ scene.json
```

也支持更简单的目录形式：

```text
case-root/
├─ lung.nii
└─ surface.obj
```

说明：

- 当前如果同一个病例目录里同时存在 DICOM 和 NIfTI，程序会优先采用 DICOM 作为主影像数据
- 直接打开单个 NIfTI 文件时，程序会尝试扫描该文件所在目录中的同级模型文件
- “直接打开影像文件”入口目前仅支持 NIfTI，不支持单个 DICOM 文件

## 目录结构

```text
.
├─ src/
│  ├─ core/          # 病例扫描、影像/模型加载
│  ├─ model/         # 数据结构定义
│  └─ view/          # 四窗格界面、MPR、3D 视图与联动控制
├─ tests/            # 逻辑测试
├─ tools/            # DICOM 转 OBJ/STL 的命令行工具
├─ third_party/      # 工程依赖的本地第三方库
└─ .vscode/          # 构建、发布、调试脚本与任务
```

几个关键文件：

- `src/core/casepackagereader.cpp`：病例目录识别
- `src/core/studyloader.cpp`：DICOM、NIfTI、模型的核心加载逻辑
- `src/view/fourpaneviewer.cpp`：四窗格显示与联动
- `mainwindow.cpp`：菜单、加载入口、异步加载流程

## 依赖环境

当前工程以 Windows 开发环境为主，现有脚本也是按 Windows 配置的。

主要依赖：

- Visual Studio C++ 工具链
- CMake
- Ninja
- Qt 5.15.2 MSVC 64-bit
- VTK 9.4
- GDCM 3.0.26
- ITK 5.4.5
- ZLIB
- QuaZip-Qt5，可选

说明：

- `CMakeLists.txt` 同时兼容 Qt 5 / Qt 6 的查找方式
- 但仓库中的 `.vscode` 构建与发布脚本当前按 Qt 5.15.2 和本机路径写死
- 如果你的 Qt、Visual Studio 或 CMake 安装位置不同，需要先修改 `.vscode/*.cmd` 中的路径变量

## 构建

### 方式一：使用 VS Code 任务

仓库已经提供了可直接运行的任务：

- `build-debug`
- `build-release`
- `publish-release`
- `publish-release-slim`

在 VS Code 中执行：

1. `Ctrl+Shift+P`
2. 运行 `Tasks: Run Task`
3. 选择对应任务

### 方式二：直接执行脚本

在工程根目录下运行：

```bat
.vscode\build-debug-msvc.cmd
.vscode\build-release-msvc.cmd
```

脚本会完成：

- 调用 Visual Studio 开发者命令环境
- 使用 CMake + Ninja 生成工程
- 构建 Debug 或 Release 版本

构建输出目录：

- `build\debug`
- `build\release`

## 运行

### 开发版运行

Debug 版：

```bat
build\debug\DicomViewerSkeleton.exe
```

Release 版：

```bat
build\release\DicomViewerSkeleton.exe
```

### 程序内使用方式

启动后可通过菜单：

- `文件 -> 打开病例包目录`
- `文件 -> 打开影像文件`

建议使用方式：

- DICOM 数据优先使用“打开病例包目录”
- 单个 NIfTI 文件可使用“打开影像文件”
- 想看影像与模型联动时，尽量把影像和模型放在同一个病例目录里

## 测试

工程包含一个逻辑测试程序：

- `dicomviewer_logic_tests`

构建后可直接运行：

```bat
build\debug\dicomviewer_logic_tests.exe
```

或使用 CTest：

```bat
ctest --test-dir build\debug --output-on-failure
```

测试内容主要覆盖：

- 病例目录识别
- NIfTI 文件识别
- 中文路径下的 NIfTI 加载
- NIfTI 几何信息规范化
- 同目录模型联动加载

## 发布打包

当前提供两种发布包：

### 1. 兼容优先包

```bat
.vscode\publish-release-msvc.cmd
```

输出目录：

```text
release/
```

特点：

- 保留软件 OpenGL 渲染兜底
- 对目标机器环境更宽容

### 2. 瘦身包

```bat
.vscode\publish-release-slim-msvc.cmd
```

输出目录：

```text
release-slim/
```

特点：

- 去掉部分兼容性兜底文件
- 体积更小
- 更适合目标机器环境可控的场景

这两种打包方式也已经集成到 VS Code 任务中。

## 辅助工具

工程还包含两个命令行工具：

- `dicom_to_obj`
- `dicom_to_stl`

用途：

- 从 DICOM 序列中提取等值面
- 导出为 OBJ 或 STL

示例：

```bat
build\release\dicom_to_obj.exe <dicom_dir> <output_obj> [iso_value] [target_reduction]
build\release\dicom_to_stl.exe <dicom_dir> <output_stl> [iso_value]
```

## 当前限制

- 当前“直接打开影像文件”只支持 NIfTI，不支持直接打开单个 DICOM 文件
- 工程脚本对本机路径有一定依赖，换机器时通常需要先调整 `.vscode` 脚本中的路径
- 发布流程目前以 Windows 桌面环境为目标
- `meta/scene.json` 当前主要作为病例包结构中的预留文件位，是否消费其内容取决于后续业务扩展

## 后续可扩展方向

- 更完整的场景文件读写
- 更多模型格式和材质支持
- 体绘制与分割结果叠加
- 更完善的病例元数据面板
- 安装包与自动部署流程

## 许可与说明

本仓库当前更像一个内部开发工程或原型工程。若后续对外发布，建议补充：

- 许可证
- 第三方依赖许可说明
- 示例数据说明
- 版本发布记录
