#!/usr/bin/env bash
set -euo pipefail

echo "MU Linux server 0.97.20 | Cliente compatible: https://github.com/EmanuelCatania/Mu-97k-Client-Source/releases/tag/0.97.20"

DB_HOST=${DB_HOST:-mysql}
DB_PORT=${DB_PORT:-3306}
DB_USER=${DB_USER:-admin}
DB_PASS=${DB_PASS:-asd123}
DB_NAME=${DB_NAME:-MuOnline97}
PUBLIC_IP=${PUBLIC_IP:-}
MU_DATA_PATH=${MU_DATA_PATH:-/opt/mu/Data/}

apply_db_settings() {
  local file="$1"
  if [ -f "$file" ]; then
    sed -i -E "s#^DataBaseHost=.*#DataBaseHost=tcp://${DB_HOST}#" "$file"
    sed -i -E "s#^DataBasePort=.*#DataBasePort=${DB_PORT}#" "$file"
    sed -i -E "s#^DataBaseUser=.*#DataBaseUser=${DB_USER}#" "$file"
    sed -i -E "s#^DataBasePass=.*#DataBasePass=${DB_PASS}#" "$file"
    sed -i -E "s#^DataBaseName=.*#DataBaseName=${DB_NAME}#" "$file"
  fi
}

apply_db_settings /opt/mu/JoinServer/JoinServer.ini
apply_db_settings /opt/mu/DataServer/DataServer.ini

if [ -n "$PUBLIC_IP" ] && [ -f /opt/mu/ConnectServer/ServerList.dat ]; then
  sed -i -E "s/\"([0-9]{1,3}\.){3}[0-9]{1,3}\"/\"${PUBLIC_IP}\"/g" /opt/mu/ConnectServer/ServerList.dat
fi

if command -v nc >/dev/null 2>&1; then
  echo "Waiting for MySQL at ${DB_HOST}:${DB_PORT}..."
  until nc -z "$DB_HOST" "$DB_PORT"; do
    sleep 1
  done
fi

export MU_DATA_PATH

# Run a second GameServer process with its own startup configuration. Both
# rooms share JoinServer, DataServer and MySQL, but keep independent world and
# event state. The copy is created once per container filesystem.
if [ ! -d /opt/mu/GameServer2 ]; then
  cp -a /opt/mu/GameServer /opt/mu/GameServer2

  ROOM2_CONFIG="/opt/mu/GameServer2/Data/GameServerInfo - StartUp.dat"
  sed -i -E "s#^ServerName=.*#ServerName=SV2#" "$ROOM2_CONFIG"
  sed -i -E "s#^WindowName=.*#WindowName=Muorigins Room 2#" "$ROOM2_CONFIG"
  sed -i -E "s#^ServerCode=.*#ServerCode=1#" "$ROOM2_CONFIG"
  sed -i -E "s#^ServerPort=.*#ServerPort=55902#" "$ROOM2_CONFIG"
  sed -i -E "s#^ServerMaxUserNumber=.*#ServerMaxUserNumber=60#" "$ROOM2_CONFIG"

  # Room 2 is the non-PvP room. Keep Room 1 unchanged as the PvP room.
  ROOM2_COMMON="/opt/mu/GameServer2/Data/GameServerInfo - Common.dat"
  sed -i -E "s#^NonPK=.*#NonPK=1#" "$ROOM2_COMMON"

  # Do not re-enable PvP inside the room's standard events.
  ROOM2_EVENT="/opt/mu/GameServer2/Data/GameServerInfo - Event.dat"
  sed -i -E "s#^BloodCastleAllowPK=.*#BloodCastleAllowPK=0#" "$ROOM2_EVENT"
  sed -i -E "s#^DevilSquareAllowPK=.*#DevilSquareAllowPK=0#" "$ROOM2_EVENT"
fi

cd /opt/mu/ConnectServer
./ConnectServer &

cd /opt/mu/JoinServer
./JoinServer &

cd /opt/mu/DataServer
./DataServer &

cd /opt/mu/GameServer
./GameServer &

cd /opt/mu/GameServer2
./GameServer &

wait -n
