@REM g++ test_client.cpp -o test_client -Wall -Wextra -g3 -O0 -Isdk_160/public/steam -Lsdk_160/public/steam/lib/win64 -Lsdk_160\redistributable_bin\win64 -lsteam_api64
@REM g++ test_server.cpp -o test_server -Wall -Wextra -g3 -O0 -Isdk_160/public/steam -Lsdk_160/public/steam/lib/win64 -Lsdk_160\redistributable_bin\win64 -lsteam_api64

clang-cl test_client.cpp steam_api64.lib /Fe"test_client" /Zi /Ot /I sdk_160/public/steam
clang-cl test_server.cpp steam_api64.lib /Fe"test_server" /Zi /Ot /I sdk_160/public/steam
