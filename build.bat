@echo off
setlocal EnableDelayedExpansion

set "config=%~1"
if "%config%"=="" set "config=Release"

set "root=%~dp0"
set "src=%root%src"
if defined CLAUDEBAWT_OUT (
    set "out=%CLAUDEBAWT_OUT%"
) else (
    set "out=%root%dist"
)
set "obj=%out%\obj"
set "imgui=%root%vendor\imgui"
set "minhook=%root%vendor\minhook"

where cl.exe >nul 2>&1
if not errorlevel 1 goto have_cl

set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" exit /b 1

for /f "usebackq tokens=*" %%I in (
    `"%vswhere%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvarsall.bat`
) do (
    call "%%I" x64
    goto have_cl
)
exit /b 1

:have_cl
if not exist "%out%" mkdir "%out%"
if not exist "%obj%" mkdir "%obj%"


set "flags=/nologo /W4 /WX- /MP /O2 /Ob3 /Oi /Ot /GL /Gy /GF /arch:AVX2 /MD /DNDEBUG /DWIN32 /D_WINDOWS /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0A00"
set "linkflags=/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /RELEASE /PDB:ClaudeBawt.pdb"
if /i "%config%"=="Debug" (
    set "flags=/nologo /W4 /WX- /MP /Od /Zi /MDd /DDEBUG /DWIN32 /D_WINDOWS /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0A00"
    set "linkflags=/DEBUG /PDB:ClaudeBawt.pdb /INCREMENTAL:NO"
)

set "inc=/I"%src%" /I"%imgui%" /I"%imgui%\backends" /I"%minhook%\include" /I"%minhook%\src""

set "cpp_sources="%src%\dllmain.cpp" "%src%\hooks.cpp" "%src%\overlay.cpp" "%src%\menu.cpp" "%src%\skin_catalog.cpp" "%src%\config.cpp" "%src%\il2cpp.cpp" "%src%\util.cpp" "%src%\log.cpp""
set "cpp_sources=%cpp_sources% "%src%\features\features.cpp" "%src%\features\aim.cpp" "%src%\features\dodge.cpp" "%src%\features\speedhack.cpp" "%src%\features\loot.cpp" "%src%\features\glow.cpp" "%src%\features\fame.cpp" "%src%\features\noclip.cpp" "%src%\features\hud.cpp" "%src%\features\socketfu.cpp" "%src%\features\puppeteer.cpp" "%src%\features\follow.cpp""
set "cpp_sources=%cpp_sources% "%src%\features\render_projectiles.cpp" "%src%\features\render_tiles.cpp" "%src%\features\render_hitbox.cpp" "%src%\features\render_safety.cpp" "%src%\features\render_units_grid.cpp""
set "cpp_sources=%cpp_sources% "%src%\features\binds_overlay.cpp""
set "cpp_sources=%cpp_sources% "%src%\features\notifications.cpp""
set "cpp_sources=%cpp_sources% "%src%\features\interactive_map.cpp""
set "cpp_sources=%cpp_sources% "%imgui%\imgui.cpp" "%imgui%\imgui_draw.cpp" "%imgui%\imgui_tables.cpp" "%imgui%\imgui_widgets.cpp""
set "cpp_sources=%cpp_sources% "%imgui%\backends\imgui_impl_dx11.cpp" "%imgui%\backends\imgui_impl_win32.cpp""

set "c_sources="%minhook%\src\buffer.c" "%minhook%\src\hook.c" "%minhook%\src\trampoline.c" "%minhook%\src\hde\hde64.c""

pushd "%out%"
cl %flags% /std:c11 %inc% /c %c_sources% /Fo:"%obj%\\"
if errorlevel 1 ( popd & exit /b 1 )
cl %flags% /EHsc /std:c++17 %inc% /c %cpp_sources% /Fo:"%obj%\\"
if errorlevel 1 ( popd & exit /b 1 )
link /nologo /DLL /OUT:ClaudeBawt.dll %linkflags% "%obj%\*.obj" kernel32.lib user32.lib gdi32.lib shell32.lib shcore.lib d3d11.lib dxgi.lib winhttp.lib crypt32.lib windowscodecs.lib ole32.lib /MACHINE:X64
set "result=%errorlevel%"
popd

if not "%result%"=="0" exit /b %result%
if not exist "%out%\font" mkdir "%out%\font"
copy /Y "%root%vendor\font\PixelOperator-Bold.ttf" "%out%\font\PixelOperator-Bold.ttf" >nul
if exist "%root%vendor\font\Regius.ttf" copy /Y "%root%vendor\font\Regius.ttf" "%out%\font\Regius.ttf" >nul
copy /Y "%root%crown.png" "%out%\crown.png" >nul
if not exist "%out%\interactive-map" mkdir "%out%\interactive-map"
copy /Y "%root%assets\interactive-map\*.png" "%out%\interactive-map\" >nul
copy /Y "%root%assets\interactive-map\ATTRIBUTION.txt" "%out%\interactive-map\ATTRIBUTION.txt" >nul
copy /Y "%out%\ClaudeBawt.dll" "%out%\client.dll" >nul
echo [build] %out%\ClaudeBawt.dll
