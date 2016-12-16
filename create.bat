@echo off

echo./*
echo. * create tengine env...
echo. */
echo.

set /p input=enter your project name :

if not exist .\bin\tengine.exe (
    echo building tengine ...
    call build.bat
)

if exist "%input%" (
    echo "%input%" project name existed ...
    goto ERROR
)

echo "creating project dir ..."

md "%input%"

pushd "%input%"

md tengine
md bin

echo "copy tengine context to project dir ..."

xcopy  /E /Y /Q ..\scripts tengine
xcopy  /E /Y /Q ..\bin bin

echo creating startup bat ...

echo .\bin\tengine.exe .\bin\tengine.conf >> start.bat

popd

:ERROR
pause

:EOF
