#pragma once
#include "sys_shared.h"
#include "tr_shared.h"
#include "ui_shared.h"
#include <SDL.h>

typedef void* ptModule;
typedef void* ptModuleFunction;

class GameModule {
	// modcode that gets loaded via shared objects
private:
	ptModule ptModuleHandle;
public:
	GameModule(string modulename);
	~GameModule();
	gameExports_s* GetRefAPI(gameImports_s* import);
};

class RaptureGame {
private:
	GameModule*		game;
	gameExports_s*	trap;
public:
	RaptureGame(int argc, char **argv);
	~RaptureGame();

	RaptureGame(const RaptureGame& other) : bHasFinished(other.bHasFinished) {}
	RaptureGame& operator= (RaptureGame other) { bHasFinished = other.bHasFinished; return *this; }

	void HandleCommandline(int argc, char **argv);
	void PassQuitEvent();

	bool bHasFinished;
	void RunLoop();

	void CreateGameModule();
};

template<typename T>
struct CvarValueSet {
	T defaultVal;
	T currentVal;

	void AssignBoth(T value) { defaultVal = currentVal = value; }
};

class Cvar {
	string name;
	string description;
	union {
		CvarValueSet<char*> s;
		CvarValueSet<int> i;
		CvarValueSet<float> v;
		CvarValueSet<bool> b;
	};
public:
	enum cvarType_e {
		CV_STRING,
		CV_INTEGER,
		CV_FLOAT,
		CV_BOOLEAN
	};
	enum cvarFlags_e {
		CVAR_ARCHIVE,
		CVAR_ROM,
		CVAR_ANNOUNCE,
	};
private:
	cvarType_e type;
	int flags;
	void AssignHardValue(char* value) { s.AssignBoth(value); }
	void AssignHardValue(int value) { i.AssignBoth(value); }
	void AssignHardValue(float value) { v.AssignBoth(value); }
	void AssignHardValue(bool value) { b.AssignBoth(value); }
	bool registered;
public:
	Cvar(Cvar&& other);
	Cvar(const string& sName, const string& sDesc, int iFlags, char* startValue);
	Cvar(const string& sName, const string& sDesc, int iFlags, int startValue);
	Cvar(const string& sName, const string& sDesc, int iFlags, float startValue);
	Cvar(const string& sName, const string& sDesc, int iFlags, bool startValue);
	Cvar();
	~Cvar();
	Cvar& operator= (char* value);
	Cvar& operator= (int value);
	Cvar& operator= (float value);
	Cvar& operator= (bool value);

	cvarType_e GetType() { return type; }
	int GetFlags() { return flags; }
	string GetName() { return name; }
	string GetDescription() { return description; }

	void SetValue(char* value) { if(type != CV_STRING) return; s.currentVal = value; if(flags & CVAR_ANNOUNCE) R_Printf("%s changed to %s\n", name.c_str(), value); }
	void SetValue(int value) { if(type != CV_INTEGER) return; i.currentVal = value; if(flags & CVAR_ANNOUNCE) R_Printf("%s changed to %i\n", name.c_str(), value); }
	void SetValue(float value) { if(type != CV_FLOAT) return; v.currentVal = value; if(flags & CVAR_ANNOUNCE) R_Printf("%s changed to %f\n", name.c_str(), value); }
	void SetValue(bool value) { if(type != CV_BOOLEAN) return; b.currentVal = value; if(flags & CVAR_ANNOUNCE) R_Printf("%s changed to %s\n", name.c_str(), btoa(value)); }
	inline char* String() { return s.currentVal; }
	inline int Integer() { return i.currentVal; }
	inline float Value() { return v.currentVal; }
	inline bool Bool() { return b.currentVal; }
	inline char* DefaultString() { return s.defaultVal; }
	inline int DefaultInteger() { return i.defaultVal; }
	inline float DefaultValue() { return v.defaultVal; }
	inline bool DefaultBool() { return b.defaultVal; }

	template<typename T>
	static Cvar* Get(const string& sName, const string& sDesc, int iFlags, T startValue) {
		try {
			auto it = CvarSystem::cvars.find(sName);
			if(it == CvarSystem::cvars.end())
				throw out_of_range("not registered");
			Cvar* cv = it->second;
			if(!cv->registered)
				throw out_of_range("not registered");
			return cv;
		}
		catch( out_of_range e ) {
			// register a new cvar
			return CvarSystem::RegisterCvar(sName, sDesc, iFlags, startValue);
		}
	}

	static bool Exists(const string& sName);
friend class CvarSystem;
friend class Cvar;
};

// Whenever we use /set or /seta on nonexisting cvars, those cvars are chucked into the cache
struct CvarCacheObject {
	string initvalue;
	bool archive;
};

class CvarSystem {
	static unordered_map<string, Cvar*> cvars;
	static unordered_map<string, CvarCacheObject*> cache;
	static bool init;
	static void Cache_Free(const string& sName);
	static void ArchiveCvars();
public:
	static string CvarSystem::GetNextCvar(const string& previous, bool& bFoundCommand);
	static string CvarSystem::GetFirstCvar(bool& bFoundCommand);

	static Cvar* RegisterCvar(Cvar *cvar);
	static Cvar* RegisterCvar(const string& sName, const string& sDesc, int iFlags, char* startValue);
	static Cvar* RegisterCvar(const string& sName, const string& sDesc, int iFlags, int startValue);
	static Cvar* RegisterCvar(const string& sName, const string& sDesc, int iFlags, float startValue);
	static Cvar* RegisterCvar(const string& sName, const string& sDesc, int iFlags, bool startValue);
	static void CacheCvar(const string& sName, const string& sValue, bool bArchive = false);
	static void Initialize();
	static void Destroy();

	static int GetCvarFlags(const string& sName) { return cvars[sName]->flags; }
	static void SetCvarFlags(const string& sName, int flags) { cvars[sName]->flags = flags; }
	static Cvar::cvarType_e GetCvarType(const string& sName) { return cvars[sName]->type; }

	static void SetStringValue(const string &sName, char* newValue) { cvars[sName]->s.currentVal = newValue; }
	static void SetIntegerValue(const string &sName, int newValue) { cvars[sName]->i.currentVal = newValue; }
	static void SetFloatValue(const string &sName, float newValue) { cvars[sName]->v.currentVal = newValue; }
	static void SetBooleanValue(const string &sName, bool newValue) { cvars[sName]->b.currentVal = newValue; }

	static string GetStringValue(const string& sName);
	static int GetIntegerValue(const string& sName);
	static float GetFloatValue(const string& sName);
	static bool GetBooleanValue(const string& sName);

	static void ListCvars();

	static bool ProcessCvarCommand(const string& sName, const vector<string>& VArguments);
friend class Cvar;
};

/* Memory */
namespace Zone {
	enum zoneTags_e {
		TAG_NONE,
		TAG_CVAR,
		TAG_FILES,
		TAG_RENDERER,
		TAG_IMAGES,
		TAG_MATERIALS,
		TAG_CUSTOM, // should only be used by mods
		TAG_MAX,
	};

	extern string tagNames[];

	struct ZoneChunk {
		size_t memInUse;
		bool isClassObject;

		ZoneChunk(size_t mem, bool bClass) : 
		memInUse(mem), isClassObject(bClass) {}
		ZoneChunk() { memInUse = 0; isClassObject = false; }
	};

	struct ZoneTag {
		size_t zoneInUse;
		size_t peakUsage;
		map<void*, ZoneChunk> zone;

		ZoneTag() : zoneInUse(0), peakUsage(0) { }
	};

	class MemoryManager {
	private:
		unordered_map<string, ZoneTag> zone;
	public:
		void* Allocate(int iSize, zoneTags_e tag);
		void Free(void* memory);
		void FastFree(void* memory, const string& tag);
		void FreeAll(const string& tag);
		void* Reallocate(void *memory, size_t iNewSize);
		void CreateZoneTag(const string& tag) { zone[tag] = ZoneTag(); }
		MemoryManager();
		~MemoryManager(); // deliberately ignoring rule of three
		void PrintMemUsage();

		template<typename T>
		T* AllocClass(zoneTags_e tag) {
			T* retVal = new T();
			ZoneChunk zc(sizeof(T), true);
			zone[tagNames[tag]].zoneInUse += sizeof(T);
			if(zone[tagNames[tag]].zoneInUse > zone[tagNames[tag]].peakUsage) {
				zone[tagNames[tag]].peakUsage = zone[tagNames[tag]].zoneInUse;
			}
			zone[tagNames[tag]].zone[retVal] = zc;
			return retVal;
		}
	};

	extern MemoryManager *mem;

	void Init();
	void Shutdown();

	void* Alloc(int iSize, zoneTags_e tag);
	void  Free(void* memory);
	void  FastFree(void *memory, const string& tag);
	void  FreeAll(const string& tag);
	void* Realloc(void *memory, size_t iNewSize);
	template<typename T>
	T* New(zoneTags_e tag) { return mem->AllocClass<T>(tag); }
	void MemoryUsage();
};

namespace Hunk {
	class MemoryManager {
	public:
		void *Allocate(int iSize);
		void Free(void* memory);
	};

	void Init();
	void Shutdown();
};

/* Filesystem */
class File {
	FILE* handle;
	string searchpath;
public:
	static File* Open(const string &file, const string& mode);
	void Close();
	string ReadPlaintext(size_t numchar = 0);
	size_t ReadBinary(unsigned char* bytes, size_t numbytes = 0);
	wstring ReadUnicode(size_t numchars = 0);
	size_t GetSize();
	inline size_t GetUnicodeSize() { return GetSize()/2; }
	size_t WritePlaintext(const string& text) { return fwrite(text.c_str(), sizeof(char), text.length(), handle); }
	size_t WriteUnicode(const wstring& text) { return fwrite(text.c_str(), sizeof(wchar_t), text.length(), handle); }
	size_t WriteBinary(void* bytes, size_t numbytes) { return fwrite(bytes, sizeof(unsigned char), numbytes, handle); }
	bool Seek(long offset, int origin) { if(fseek(handle, offset, origin)) return true; return false; }
	size_t Tell() { return ftell(handle); }
	static string GetFileSearchPath(const string& fileName);
	bool Exists(const string &file);
friend class File;
};

namespace FS {
	class FileSystem {
		vector<string> searchpaths;
		unordered_map<string, File*> files;
		void RecursivelyTouch(const string& path);
	public:
		void AddSearchPath(const string& searchpath) { string s = searchpath; stringreplace(s, "\\", "/"); searchpaths.push_back(s); }
		inline void AddCoreSearchPath(const string& basepath, const string& core) { AddSearchPath(basepath + '/' + core); }
		void CreateModSearchPaths(const string& basepath, const string& modlist);
		FileSystem();
		~FileSystem();
		inline vector<string>& GetSearchPaths() { return searchpaths; }
		void PrintSearchPaths();
		static int ListFiles(const string& dir, vector<string>& in, const string& extension="");
	friend class File;
	};

	void Init();
	void Shutdown();

	void* EXPORT_OpenFile(const char* filename, const char* mode);
	void EXPORT_Close(void* filehandle);
	int EXPORT_ListFilesInDir(const char* filename, vector<string>& in, const char *extension);
	string EXPORT_ReadPlaintext(void* filehandle, size_t numChars);
	size_t EXPORT_GetFileSize(void* filehandle);
};

/* CmdSystem.cpp */
namespace Cmd {
	void ProcessCommand(const char *cmd);
	void AddCommand(const string& cmdName, conCmd_t cmd);
	void RemoveCommand(const string& cmdName);
	void ClearCommandList();
	void ListCommands();
	vector<string> Tokenize(const string &str);
	string TabComplete(const string& input);
};

/* Input.cpp */
class InputManager {
private:
	map<SDL_Scancode, string> binds;
	list<SDL_Scancode> thisFrameKeysDown;
	void BindCommandMouse(const string& keycodeArg, const string& commandArg);
	void (*Keycatcher)(SDL_Keycode key);
public:
	void SendKeyUpEvent(SDL_Keysym key, char* text);
	void SendKeyDownEvent(SDL_Keysym key, char* text);
	void InputFrame();
	void ExecuteBind(const string& bindName);
	void BindCommand(const string& keycodeArg, string commandArg);
	void SendMouseButtonEvent(unsigned int buttonId, unsigned char state, int x, int y);
	void SendMouseMoveEvent(int x, int y);
	void SendTextInputEvent(char* text);
	InputManager();
};

extern InputManager *Input;
void InitInput();
void DeleteInput();
extern const string keycodeNames[];

// sys_cmds.cpp
void Sys_InitCommands();

// sys_<platform>.cpp
char* Sys_FS_GetHomepath();
char* Sys_FS_GetBasepath();
string Sys_GetClipboardContents();
void Sys_SendToClipboard(string text);
void Sys_FS_MakeDirectory(const char* path);
ptModule Sys_LoadLibrary(string name);
ptModuleFunction Sys_GetFunctionAddress(ptModule module, string name);
bool Sys_Assertion(const char* msg, const char* file, const unsigned int line);
void Sys_Error(const char* error, ...);
void Sys_ShowConsole(int vislevel, bool bQuitOnExit);
void Sys_CreateConsole( void );
void Sys_PassToViewlog(const char* text);
void Sys_ClearConsoleWindow();