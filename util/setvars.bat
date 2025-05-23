
for /f "tokens=1,2 delims==" %%A in (%~dp0vars.txt) do (
    set %%A=%%B
)

