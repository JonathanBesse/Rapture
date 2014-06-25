#pragma once
#include <sys_shared.h>
#include "../json/cJSON.h"
#include "QuadTree.h"

extern gameImports_s* trap;
#define R_Printf trap->printf
#define R_Error trap->error

#define MAX_HANDLE_STRING	64

// cg
void RegisterMedia();
void DrawViewportInfo();

extern int currentMouseX;
extern int currentMouseY;

// cg_fps.cpp
void InitFPS();
void FPSFrame();
float GetGameFPS();
unsigned int GetGameFrametime();

// cg_hud.cpp
void InitHUD();
void ShutdownHUD();
void HUD_EnterArea(const char* areaName);
void HUD_DrawLabel(const char* labelText);
void HUD_HideLabels();


// g_shared.cpp
void eswap(unsigned short &x);
void eswap(unsigned int &x);
string genuuid();

// JSON parser structs
typedef function<void (cJSON*, void*)> jsonParseFunc;
bool JSON_ParseFieldSet(cJSON* root, const unordered_map<const char*, jsonParseFunc>& parsers, void* output);
bool JSON_ParseFile(char *filename, const unordered_map<const char*, jsonParseFunc>& parsers, void* output);

// Generic Cache class
template<class T>
class Cache {
private:
	size_t elementsAllocated;
	size_t elementsUsed;
	T* cache;

	vector<int> ivInUse;
public:
	void Flush(bool bFlushInUseOnly = false);
	void Insert(T* element, bool bInUse = false);
	T* GetElement(const int at) { if(at >= elementsUsed) return NULL; return &cache[at] }
	Cache(const string& zoneTag);
};

// Shader element types
enum ShaderMapType_e {
	SMT_DIFFUSE,
	SMT_SPECULAR,
	SMT_NORMAL
};

////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  MAP SYSTEM
//
////////////////////////////////////////////////////////////////////////////////////////////////////////
#define MAX_MAP_LINKS	8
typedef signed int coords_t[2];
enum tileRenderType_e {
	TRT_FLOOR1,
	TRT_FLOOR2,
	TRT_FLOOR3,
	TRT_WALL1,
	TRT_WALL2,
	TRT_WALL3,
	TRT_SHADOW,		// FIXME: not needed?
	TRT_SPECIAL
};

// Each tile is placed onto a map. The tiles contain 16 subtiles
struct Tile {
	// Basic info
	char name[MAX_HANDLE_STRING];

	// Subtile flags
	unsigned short lowmask;		// Mask of subtiles which block the player (but can be jumped over)
	unsigned short highmask;	// Mask of subtiles which block the player (and cannot be jumped over)
	unsigned short lightmask;	// Mask of subtiles which block light
	unsigned short vismask;		// Mask of subtiles which block visibility

	// Material
	Material* materialHandle;	// Pointer to material in memory
	bool bResourcesLoaded;		// Have the resources (images/shaders) been loaded?

	// Depth
	bool bAutoTrans;
	int iAutoTransX, iAutoTransY, iAutoTransW, iAutoTransH;
	float fDepthScoreOffset;

	Tile() {
		name[0] = lowmask = highmask = lightmask = vismask = '\0';
		fDepthScoreOffset = 0.0f;
		iAutoTransX = iAutoTransY = iAutoTransW = iAutoTransH = 0;
		materialHandle = NULL;
		bResourcesLoaded = bAutoTrans = false;
	}
};

// An entity is an object in the world, such as a monster
class Entity;
class SpatialEntity : public QTreeNode<float> {
protected:
	string uuid;
public:
	SpatialEntity(Entity* from);
	SpatialEntity();
friend struct Worldspace;
};

class Entity : public SpatialEntity {
public:
	QuadTree<SpatialEntity, float>* ptContainingTree;
	SpatialEntity* ptSpatialEntity;
	virtual void render() = 0;
	virtual void think() = 0;
	virtual void spawn() = 0;
};

// Actors have visible representations, whereas regular entities do not.
class Actor : public Entity {
public:
	float GetPreviousX() const { return pX; }
	float GetPreviousY() const { return pY; }
protected:
	Material* materialHandle;
	float pX, pY;
friend struct Worldspace;
};

// Players are...players.
class Player : public Actor {
	unsigned char playerNum;
	void MoveToScreenspace(int sx, int sy, bool bStopAtDestination);
public:
	virtual void render();
	virtual void think();
	virtual void spawn();
	Player(float x, float y);

	void MouseUpEvent(int sX, int sY);
	void MouseDownEvent(int sX, int sY);
	void MouseMoveEvent(int sX, int sY);
};

// Other entities..
class info_player_start : public Entity {
private:
	bool bVis0;
	bool bVis1;
	bool bVis2;
	bool bVis3;
	bool bVis4;
	bool bVis5;
	bool bVis6;
	bool bVis7;

public:
	virtual void render() { }
	virtual void think();
	virtual void spawn();
};

// A tilenode is a tile in the world
class TileNode : public QTreeNode<unsigned int> {
public:
	Tile* ptTile;
	tileRenderType_e rt;

	TileNode() {
		w = h = 1;
	}
};

// Provides a framework for the Map to be built.
struct MapFramework {
	char name[MAX_HANDLE_STRING];
	bool bIsPreset;
	int nDungeonLevel;
	string sLink[MAX_MAP_LINKS];

	char entryPreset[MAX_HANDLE_STRING];
};
extern vector<MapFramework> vMapData;

// Each map belongs to the worldspace, consuming a portion of it.
struct Map {
	char name[MAX_HANDLE_STRING];
	bool bIsPreset;					// Is this map a preset level? (Y/N)
	vector<TileNode> tiles;
	vector<SpatialEntity> ents;
};

// g_preset.cpp
struct PresetFileData {
	// Header data
	struct PFDHeader {
		char header[8];				// 'DRLG.BDF'
		unsigned short version;		// 1 - current version
		char lookup[MAX_HANDLE_STRING];
		unsigned long reserved1;		// Not used.
		unsigned long reserved2;		// Not used.
		unsigned short reserved3;	// Not used.
		unsigned long sizeX;
		unsigned long sizeY;
		unsigned long numTiles;
		unsigned long numEntities;
	};
	PFDHeader head;

	// Tiles
#pragma pack(1)
	struct LoadedTile {
		char lookup[MAX_HANDLE_STRING];
		signed int x;
		signed int y;
		unsigned char rt;
		signed char layerOffset;
	};
	LoadedTile* tileBlocks;

	// Entities
#pragma pack(1)
	struct LoadedEntity {
		char lookup[MAX_HANDLE_STRING];
		float x;
		float y;
		unsigned int spawnflags;
		unsigned int reserved1;
	};
	LoadedEntity* entities;
};
void MP_LoadTilePresets();
PresetFileData* MP_GetPreset(const string& sName);
void MP_AddToMap(const char* pfdName, Map& map);

// The map loader gets initialized by the game first, and then the worldspace grabs map data from it
class MapLoader {
private:
	vector<Tile> vTiles;
	unordered_map<string, vector<Tile>::iterator> mTileResolver;
	vector<Map> vMaps;
	unordered_map<string, vector<Map>::iterator> mMapResolver;

	void LoadTile(void* file, const char* filename);
public:
	MapLoader(const char* presetsPath, const char* tilePath);
	void BeginLoad(unsigned int levelId);
	~MapLoader();

	Tile* FindTileByName(const string& str) {
		auto it = mTileResolver.find(str);
		if(it == mTileResolver.end()) {
			return NULL;
		}
		return &(*(it->second)); // HACK
	}
};
void InitLevels();
void ShutdownLevels();
extern MapLoader* maps;

// The worldspace resembles an entire act's maps, all crammed into a gigantic grid
struct Worldspace {
public:
	Worldspace();
	~Worldspace();
	QuadTree<SpatialEntity, float>* qtEntTree;
	QuadTree<TileNode, unsigned int>* qtTileTree;

	void InsertInto(Map* theMap);
	void SpawnEntity(Entity* ent, bool bShouldRender, bool bShouldThink, bool bShouldCollide);
	void AddPlayer(Player* ptPlayer);
	Player* GetFirstPlayer(); // temphack

	void Run();
	void Render();

	static float WorldPlaceToScreenSpaceFX(float x, float y);
	static float WorldPlaceToScreenSpaceFY(float x, float y);
	static int WorldPlaceToScreenSpaceIX(int x, int y);
	static int WorldPlaceToScreenSpaceIY(int x, int y);
	static float ScreenSpaceToWorldPlaceX(int x, int y);
	static float ScreenSpaceToWorldPlaceY(int x, int y);

	float PlayerOffsetX();
	float PlayerOffsetY();

	void UpdateEntities();
	void ActorMoved(Actor* ptActor);
private:
	unordered_map<string, Entity*> mRenderList;
	unordered_map<string, Entity*> mThinkList;
	unordered_map<string, Entity*> mCollideList;

	vector<Player*> vPlayers;
	vector<Actor*> vActorsMoved;
};
extern Worldspace world;