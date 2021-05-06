echo Updating addons_contrib submodule 
if not "%GIT%" == "" (
    cd "%BLENDER_DIR%\release\scripts\addons_contrib"
    if errorlevel 1 goto FAIL
    "%GIT%" checkout umm_add_on
    if errorlevel 1 goto FAIL
    "%GIT%" pull --rebase origin umm_add_on
    if errorlevel 1 goto FAIL
    goto EOF
) else (
    echo Git not found in path
    goto FAIL
)

goto EOF

:FAIL
exit /b 1
:EOF