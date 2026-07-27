@echo off

python mach build

if errorlevel 1 exit /b 1

if not exist C:\Forge\bin mkdir C:\Forge\bin

copy /Y obj-spider\dist\bin\forge.exe C:\Forge\bin\forge.exe

echo.
echo Forge installed successfully.