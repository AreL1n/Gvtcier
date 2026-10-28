#pragma once
#include "bicu/core/export.h"
#include <cstdint>

struct Scene;
struct Player;

typedef void (*GvtcierNetRpcFn)(void* user, uint32_t peerId, const char* name, const void* data, int len);

GVTCIER_API bool Gvtcier_Net_Init(uint16_t defaultPort);
GVTCIER_API void Gvtcier_Net_Shutdown();

GVTCIER_API bool Gvtcier_Net_StartServer(uint16_t port, const char* room, int maxPeers);
GVTCIER_API bool Gvtcier_Net_StartClient(uint16_t port, const char* room);
GVTCIER_API void Gvtcier_Net_Stop();

GVTCIER_API bool Gvtcier_Net_ConnectDirect(const char* host, uint16_t port);
GVTCIER_API bool Gvtcier_Net_ConnectSignal(const char* host, uint16_t port, const char* room, bool forceRelay);
GVTCIER_API bool Gvtcier_Net_RegisterRoom(const char* host, uint16_t port, bool forceRelay);
GVTCIER_API void Gvtcier_Net_Disconnect();

GVTCIER_API int Gvtcier_Net_ScanRooms(char* names, int nameStride, int cap, uint16_t* ports);

GVTCIER_API void Gvtcier_Net_SetScene(Scene* scene);
GVTCIER_API void Gvtcier_Net_SetPlayer(Player* player);
GVTCIER_API void Gvtcier_Net_SetRpcHandler(GvtcierNetRpcFn fn, void* user);
GVTCIER_API void Gvtcier_Net_SetSyncPlayer(bool on);

GVTCIER_API void Gvtcier_Net_AddEntity(const char* name);
GVTCIER_API void Gvtcier_Net_RemoveEntity(const char* name);
GVTCIER_API void Gvtcier_Net_ClearEntities();
GVTCIER_API void Gvtcier_Net_SendRpc(const char* name, const void* data, int len, uint32_t target);

GVTCIER_API void Gvtcier_Net_Tick(float dt);

GVTCIER_API uint16_t Gvtcier_Net_Port();
GVTCIER_API uint32_t Gvtcier_Net_SelfId();
GVTCIER_API int Gvtcier_Net_PeerCount();
GVTCIER_API int Gvtcier_Net_PlayerCount();
GVTCIER_API bool Gvtcier_Net_IsReady();
GVTCIER_API bool Gvtcier_Net_IsServer();
GVTCIER_API bool Gvtcier_Net_IsRelayed();
GVTCIER_API float Gvtcier_Net_RttMs();
GVTCIER_API const char* Gvtcier_Net_Room();
