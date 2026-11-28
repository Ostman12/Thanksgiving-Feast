# Thanksgivng-Feast

Feast Mode: a Thanksgiving party game for 4-10 players on their phones.
See `docs/rules.md`, `docs/protocol.md` and `docs/design.md`.

## Build (Windows, Visual Studio 2022)

The first configure downloads Crow, Asio and nlohmann/json (needs internet once).

```powershell
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S . -B build -G "Visual Studio 17 2022"
& $cmake --build build --config Release
```

## Test

```powershell
.\build\Release\feast_tests.exe        # add -v to print each test name
```

## Run

From the repo root (so `client/` is found):

```powershell
.\build\Release\feast_server.exe --port 8080              # 4+ players to start
.\build\Release\feast_server.exe --port 8080 --test-mode  # 2+ players to start
.\build\Release\feast_server.exe --no-around-the-table    # leave Game 3 out of the match
```

Flags combine, e.g. `--test-mode --no-around-the-table`. Restarting the server clears all
rooms, so change flags between matches.

Phones open `http://<laptop-ip>:8080/` (or the tunnel URL). The big screen opens
`/display`. Windows may ask to allow the server through the firewall the first
time: allow it on private networks so phones on the Wi-Fi can connect.
