@echo off
cd /d %~dp0
set proj_name=%1
if "%proj_name%"=="" echo 请传入文件名&pause&exit
echo 1 > ..\obj\ram.o
@echo on
REM riscv32-elf-objdump -h -d -t %proj_name%.rv32 > %proj_name%.lst || goto err
riscv32-elf-objcopy -O binary %proj_name%.rv32 app.bin || goto err
riscv32-elf-xmaker %proj_name%.xm
if exist C:\upload\upload.bat       (call C:\upload\upload.bat -D SmartMin %proj_name%.dcf)
exit
:err
@echo off
if "%1"=="" pause
exit /b 1