#pragma once
#include "sys_local.h"
#include "tr_shared.h"
#include <SDL.h>
#include <SDL_image.h>
#include <gl/GL.h>
#include <gl/GLU.h>

extern Cvar* r_fullscreen;
extern Cvar* r_width;
extern Cvar* r_height;
extern Cvar* r_windowtitle;
extern Cvar* r_gamma;

void CreateConsole();
void DestroyConsole();
void ShowConsole();
void HideConsole();

// MaterialHandler.cpp
// Anything that exists in the world uses a Material.
// Images and the like do not use Materials since they do not exist in the world.
class Material {
private:
	bool bLoadedResources; // Have we loaded resources? (images, etc)
	bool bLoadedIncorrectly; // If we loaded, did we load correctly?
	void LoadResources();
	void FreeResources();

	char name[64];
	char resourceFile[64];
	SDL_Surface *ptResource;
public:
	Material();
	~Material();
	void SendToRenderer(float x, float y);

friend class MaterialHandler;
};

class MaterialHandler {
private:
	unordered_map<string, Material*> materials;
	void LoadMaterial(const char* materialFile);
public:
	Material* GetMaterial(const char* material);
	~MaterialHandler();
	MaterialHandler();
};

extern MaterialHandler* mats;