1. 使用时，请在头文件之后添加
   ```cpp
   #pragma comment(lib, "hiredis.lib")
   ```
2. 在项目属性页，设置好对应的包含目录（include）和库目录（lib）
3. `代码生成-运行库`，选择MTd
4. `链接器-常规-附加库目录`，将hiredis的lib目录也包含进去
# 关于找不到`hiredis.dll`的问题，参考以下回答
你已经成功用 MinGW 或 MSVC 构建出了 `hiredis.dll` 和 `hiredis.lib`，但运行时仍然提示“找不到 hiredis.dll”，这说明你的程序在启动时**无法定位 DLL 文件的位置**。这是 Windows 下动态链接的常见问题，我们来逐步分析并解决：

---

## 🧠 为什么会提示“找不到 hiredis.dll”？

Windows 在运行时查找 DLL 的顺序如下：

1. **程序所在目录**
2. **当前工作目录**
3. **系统目录（System32）**
4. **Windows 目录**
5. **环境变量 PATH 中的所有目录**

如果 `hiredis.dll` 不在这些路径中，程序就会报错：  
```
The code execution cannot proceed because hiredis.dll was not found.
```

---

## ✅ 解决方法

### 方法一：将 `hiredis.dll` 放到程序同目录
最直接的方式：

```plaintext
your_project/
├── test.exe
├── hiredis.dll   ✅ 放在这里
```

这样 Windows 会优先在当前目录查找 DLL。

---

### 方法二：将 DLL 所在路径加入系统环境变量 PATH

1. 找到 DLL 路径，例如：
   ```
   C:\hiredis-install\bin\hiredis.dll
   ```
2. 打开系统环境变量设置：
   - Win + S → 搜索“环境变量”
   - 编辑系统变量 → 找到 `PATH` → 添加上述路径
3. 重启命令行或 IDE，再运行程序

---

### 方法三：在代码中设置 DLL 搜索路径（仅限测试）

你可以在程序中添加：
```c
SetDllDirectory("C:\\hiredis-install\\bin");
```
这会临时告诉 Windows 去这个目录找 DLL，但不建议用于生产环境。

---

## 🧪 验证 DLL 是否可加载

你可以用 `dumpbin` 或 `Dependency Walker` 工具检查：
```powershell
dumpbin /exports hiredis.dll
```
或者运行：
```powershell
where hiredis.dll
```
确认系统能找到它。

---

## 💡 建议

如果你是用 CMake 安装的 hiredis，可以统一把 DLL 安装到一个公共目录（如 `C:\libs\bin`），然后把这个目录加入 PATH，所有项目都能复用。
