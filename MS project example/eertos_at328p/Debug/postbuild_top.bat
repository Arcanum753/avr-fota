rem args: path, name, sig, proj, ver

setlocal
set vermj=
set vermn= 

for /f "usebackq tokens=1,2,3" %%i in ("%~5") do (
  if "%%i"=="#define" (
    if "%%j"=="VERSION_MAJOR" set vermj=%%k
    if "%%j"=="VERSION_MINOR" set vermn=%%k
  )
)
if exist "%~1\%~2_ota%vermj%.%vermn%.hex" del "%~1\%~2_ota%vermj%.%vermn%.hex"
echo $sign %~3>> "%~1\%~2_ota%vermj%.%vermn%.hex"
echo $proj %~4>> "%~1\%~2_ota%vermj%.%vermn%.hex"
echo $vers %vermj%.%vermn%>> "%~1\%~2_ota%vermj%.%vermn%.hex"
set _time=%time:~0,2%/%time:~3,2%/%time:~6,2%
for /f "tokens=1,2,3,4,5,6 delims=.:" %%i in ("%date%.%time%") do echo $time %%k.%%j.%%i %_time%>>"%~1\%~2_ota%vermj%.%vermn%.hex"
copy /b "%~1\%~2_ota%vermj%.%vermn%.hex"+"%~1\%~2.hex" "%~1\%~2_ota%vermj%.%vermn%.hex"

echo Done! 	