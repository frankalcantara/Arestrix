@echo off
echo >>> Compilando New Taskflow Graph com MSVC (C++20)...
cl.exe /std:c++20 /EHsc /O2 /W4 /I"include"  main.cpp /Fe:"pipeline_app.exe"
if %errorlevel% equ 0 (
    echo >>> Compilacao com sucesso!
) else (
    echo >>> Erro na compilacao!
)
