@echo off
TITLE Source File Combiner
echo Starting the file scanner...
echo.

:: Check if the python file exists before trying to run it
if not exist "combine_files.py" (
    echo ERROR: Could not find 'combine_files.py'
    echo Please make sure this batch file is in the same folder as the python script.
    goto End
)

:: Run the script
python combine_files.py

:End
echo.
echo ==================================================
echo Task Finished. Check for 'combined_source.txt'.
echo ==================================================
pause