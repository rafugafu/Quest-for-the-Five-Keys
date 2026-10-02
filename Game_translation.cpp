// Game.cpp — C++ port of Game.py.
// Compile: g++ -std=c++17 -Os -flto -s -fno-rtti -fno-exceptions -fdata-sections -ffunction-sections -Wl,--gc-sections -fno-stack-protector -fno-ident -o Game Game.cpp






/*
__________________________________________________
______________________STOP!_______________________

SPOILER ALERT

HAVE YOU PLAYED THE GAME BEFORE READING THE CODE ?
IF NOT, GO PLAY IT FIRST.

...

WHY ARE YOU STILL READING THIS?
CLOSE THE CODE.
GO PLAY THE GAME LIKE A NORMAL PLAYER.
THE CODE WILL STILL BE HERE LATER.

IF YOU HAVE, GO AHEAD!

*/







#include <string>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <variant>
#include <memory>
#include <functional>
#include <algorithm>
#include <optional>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <filesystem>
#include <fstream>

// ─── I/O helpers ───
// emit(): write a string to stdout.
// readline(): read one line (newline stripped), flushing stdout first so
// prompts appear before input. Returns false only on EOF with nothing read.
static bool colorEnabled = true;
static bool ansiEnabled = true;
static std::string stripColorAnsi(const std::string& s) {
    static const char* const codes[] = {"0m","1m","31m","32m","38;5;136m","38;5;30m","3m","4m","1;40;36m"};
    std::string r = s;
    for(const char* code : codes) {
        std::string pat = std::string("\x1b[") + code;
        size_t pos = 0;
        while((pos = r.find(pat, pos)) != std::string::npos) r.erase(pos, pat.size());
    }
    return r;
}
static std::string stripAllAnsi(const std::string& s) {
    static const char* const codes[] = {"0m","1m","31m","32m","38;5;136m","38;5;30m","3m","4m","1;40;36m","2J","3J","H","?1049h","?1049l"};
    std::string r = s;
    for(const char* code : codes) {
        std::string pat = std::string("\x1b[") + code;
        size_t pos = 0;
        while((pos = r.find(pat, pos)) != std::string::npos) r.erase(pos, pat.size());
    }
    return r;
}
static void emit(const std::string& s) {
    if(!ansiEnabled) { std::fputs(stripAllAnsi(s).c_str(), stdout); return; }
    if(!colorEnabled) { std::fputs(stripColorAnsi(s).c_str(), stdout); return; }
    std::fputs(s.c_str(), stdout);
}

// ─── --log-file game-log mirror (mirrors Game.py's printoutput()/getinput() wrappers) ───
static FILE* logfile = nullptr;
static std::string stripAnsiForLog(const std::string& s) { return stripAllAnsi(s); }
// pyprint(): emit() plus, when logging is active, log the text as a print()
// call would have (the trailing '\n' emit() calls add to mimic print()'s
// automatic newline is not part of the logged text, matching Python).
static void pyprint(const std::string& s) {
    if(logfile) {
        std::string arg = s;
        if(!arg.empty() && arg.back() == '\n') arg.pop_back();
        std::string stripped = stripAnsiForLog(arg);
        if(!stripped.empty()) {
            std::fputs("=== Game Output ===\n\n", logfile);
            std::fputs(stripped.c_str(), logfile);
            std::fputs("\n\n", logfile);
            std::fflush(logfile);
        }
    }
    emit(s);
}
// pyprintKeepNL(): like pyprint(), but for the handful of call sites where the
// C++ text literal's own trailing '\n' is part of the printed content itself
// (not the auto-newline print() adds) because a later call's leading '\n' was
// used on the Python side to supply print()'s auto-newline visually instead.
static void pyprintKeepNL(const std::string& s) {
    if(logfile) {
        std::string stripped = stripAnsiForLog(s);
        if(!stripped.empty()) {
            std::fputs("=== Game Output ===\n\n", logfile);
            std::fputs(stripped.c_str(), logfile);
            std::fputs("\n\n", logfile);
            std::fflush(logfile);
        }
    }
    emit(s);
}
static bool readline(std::string& s, const std::string& prompt = "") {
    std::fflush(stdout);
    s.clear();
    int c; bool got=false;
    while((c=std::getchar())!=EOF) { got=true; if(c=='\n') break; s.push_back((char)c); }
    if(got && logfile) {
        std::fputs(stripAnsiForLog(prompt).c_str(), logfile);
        std::fputs("\n\n=== User Input ===\n\n", logfile);
        std::fputs(s.c_str(), logfile);
        std::fputs("\n\n", logfile);
        std::fflush(logfile);
    }
    return got;
}
// exitWithError(): mirrors Game.py's exitwith() — a plain (unstripped,
// unlogged) stdout print followed by exit(1), used where Python code now
// deliberately bypasses printoutput()/logging on its way out.
static void exitWithError(const std::string& msg) {
    std::printf("%s\n", msg.c_str());
    std::exit(1);
}

// gStdinEOF: set when a live (non-replay) read hits true EOF. Python's input()
// raises an uncaught EOFError in this case (crashing); main()'s top-level
// prompt and play-again loop instead check this flag and exit cleanly,
// matching this C++ port's existing (pre-this-refactor) simplification of
// not reproducing a Python traceback. No other call site checks it, since
// none did before either.
static bool gStdinEOF = false;
// rawGetInput(): mirrors Game.py's renamed _getinput() — the original plain
// prompt+read+log, with no replay-queue awareness.
static std::string rawGetInput(const std::string& promptText) {
    std::string s;
    emit(promptText);
    if(!readline(s, promptText)) gStdinEOF = true;
    return s;
}

// gInputs/gQueuedInputs/getInputWrapped(): mirrors Game.py's module-level
// inputs/queued_inputs/getinput(). Every interactive prompt anywhere in the
// game (not just top-level commands — "press enter" waits, y/n prompts,
// dialogue option numbers, the device's "code: " prompt, ...) goes through
// this single function, so save/load can transparently record and replay
// the exact literal sequence of everything the player typed, regardless of
// which code path asked for it. File-scope (not an Engine member) because
// it must be reachable from Button::press(), which isn't part of Engine.
static std::vector<std::string> gInputs;
static std::vector<std::string> gQueuedInputs;
static std::string getInputWrapped(const std::string& promptText) {
    if(gQueuedInputs.empty()) {
        gQueuedInputs.insert(gQueuedInputs.begin(), rawGetInput(promptText));
    } else {
        // Echo the queued value as if the player had typed it, mirroring
        // Python's printoutput(string + queued_inputs[0] + "\n") — note the
        // embedded "\n" plus printoutput()'s own default trailing "\n" stack,
        // matching the established pyprint() convention of passing the full
        // "as if print() added its own newline too" text.
        pyprint(promptText + gQueuedInputs.front() + "\n\n");
    }
    std::string gotinput = gQueuedInputs.front();
    gQueuedInputs.erase(gQueuedInputs.begin());
    gInputs.push_back(gotinput);
    return gotinput;
}

// ─── command-line argument parser (mirrors Game.py's --option parser) ───
// Option value kinds: 0 = True (unset, requires a value), 1 = False (unset flag),
// 2 = None (flag was set), 3 = Str (has a string value).
struct OptVal { int kind; std::string s; };
// Returns false (and prints an error to stdout) if parsing should abort the program.
static bool parseArgs(int argc, char** argv, std::string& fileOpt, bool& fileSet, bool& noColorSet, bool& noAnsiSet, bool& loadSet) {
    std::map<std::string,OptVal> options = {{"log-file", {0,""}}, {"no-color", {1,""}}, {"no-ansi", {1,""}}, {"load", {1,""}}};
    std::string curarg; bool hasCurarg=false;
    auto startsWithDD=[](const std::string& a){ return a.size()>=2 && a[0]=='-' && a[1]=='-'; };
    for(int idx=1; idx<argc; ++idx) {
        std::string arg = argv[idx];
        if(!hasCurarg && !startsWithDD(arg)) { std::printf("error: unexpected \"%s\"\n", arg.c_str()); return false; }
        if(hasCurarg) {
            int k = options[curarg].kind;
            if((k==1||k==2) && !startsWithDD(arg)) { std::printf("error: option \"%s\" does not take any argument\n", curarg.c_str()); return false; }
        }
        if(startsWithDD(arg)) {
            if(hasCurarg) {
                int k = options[curarg].kind;
                if(!(k==1||k==2)) { std::printf("error: unexpected \"%s\" after option \"--%s\"\n", arg.c_str(), curarg.c_str()); return false; }
            }
            arg = arg.substr(2);
        }
        if(hasCurarg) {
            int k = options[curarg].kind;
            if(k==1||k==2) hasCurarg=false;
        }
        if(!hasCurarg) {
            size_t eq = arg.find('=');
            if(eq != std::string::npos) {
                std::string opn = arg.substr(0,eq), opv = arg.substr(eq+1);
                if(!options.count(opn)) { std::printf("error: unknown option \"--%s\"\n", opn.c_str()); return false; }
                OptVal& ov = options[opn];
                if(ov.kind==0) { ov.kind=3; ov.s=opv; }
                else if(ov.kind==1||ov.kind==2) { std::printf("error: option \"--%s\" does not take any argument\n", opn.c_str()); return false; }
                else { std::printf("error: repeated argument \"--%s\"\n", arg.c_str()); return false; }
            } else {
                if(!options.count(arg)) { std::printf("error: unknown option \"--%s\"\n", arg.c_str()); return false; }
                OptVal& ov = options[arg];
                if(ov.kind==0) { curarg=arg; hasCurarg=true; }
                else if(ov.kind==1) { curarg=arg; hasCurarg=true; ov.kind=2; }
                else { std::printf("error: repeated argument \"--%s\"\n", arg.c_str()); return false; }
            }
        } else {
            options[curarg].kind=3; options[curarg].s=arg; hasCurarg=false;
        }
    }
    if(hasCurarg) {
        int k = options[curarg].kind;
        if(!(k==1||k==2)) { std::printf("error: unspecified option \"%s\"\n", curarg.c_str()); return false; }
    }
    OptVal& fv = options["log-file"];
    fileSet = fv.kind != 0;
    fileOpt = fv.s;
    noColorSet = options["no-color"].kind == 2;
    noAnsiSet = options["no-ansi"].kind == 2;
    loadSet = options["load"].kind == 2;
    return true;
}

// ─────────────────────────────────────────────────────────────
// Properties map
// ─────────────────────────────────────────────────────────────
using PropVal = std::variant<
    std::monostate, std::string, bool, int, double,
    std::unordered_set<std::string>
>;
using Props = std::unordered_map<std::string, PropVal>;

static bool propHas(const Props& p, const std::string& k) { return p.count(k)>0; }
static std::string propStr(const Props& p, const std::string& k, const std::string& d="") {
    auto it=p.find(k); if(it==p.end()) return d;
    if(auto*s=std::get_if<std::string>(&it->second)) return *s;
    return d;
}
static bool propBool(const Props& p, const std::string& k, bool d=false) {
    auto it=p.find(k); if(it==p.end()) return d;
    if(auto*b=std::get_if<bool>(&it->second)) return *b;
    return d;
}
static int propInt(const Props& p, const std::string& k, int d=0) {
    auto it=p.find(k); if(it==p.end()) return d;
    if(auto*i=std::get_if<int>(&it->second)) return *i;
    return d;
}
static double propNumD(const Props& p, const std::string& k, double d=0) {
    auto it=p.find(k); if(it==p.end()) return d;
    if(auto*i=std::get_if<int>(&it->second)) return *i;
    if(auto*f=std::get_if<double>(&it->second)) return *f;
    return d;
}
static std::string fmtNum(double v) {
    if(v==(long long)v) return std::to_string((long long)v);
    char buf[32]; std::snprintf(buf, sizeof(buf), "%g", v); return buf;
}
static std::string capFirst(std::string s) {
    if(!s.empty()) s[0]=std::toupper((unsigned char)s[0]);
    return s;
}
static const std::unordered_set<std::string>* propSet(const Props& p, const std::string& k){
    auto it=p.find(k); if(it==p.end()) return nullptr;
    return std::get_if<std::unordered_set<std::string>>(&it->second);
}

// ─────────────────────────────────────────────────────────────
// EndGame
// ─────────────────────────────────────────────────────────────
struct EndGame { std::string description, endmessage; bool win; };

// ─────────────────────────────────────────────────────────────
// PASSWORDS — mirrors Game.py's module-level PASSWORDS tuple
// ─────────────────────────────────────────────────────────────
static const std::vector<std::string> PASSWORDS = {"ad6x29z","xz50op3","g8k9vbn","8dkr6e9","b4n7cc1"};
static const std::string LAST_PASSWD_CODE = "Bangalore is Four distances North in Seven of China's largest Cuisines, number One.";

// hasRealProgress(): mirrors Game.py's
// `[x for x in inputs if x.strip().lower() not in ("save","load","quit")]`
// truthiness check used by quit()/load() to decide whether there's anything
// worth asking to save/discard. gInputs (declared with getInputWrapped()
// above) lives at file scope, not on Engine, because Python's Game object —
// and so its `inputs` list — persists across "play again" restarts, while
// the C++ Engine is recreated each restart.
static bool hasRealProgress() {
    for(const auto& x : gInputs) {
        std::string low=x;
        size_t a=0,b=low.size();
        while(a<b&&isspace((unsigned char)low[a])) ++a;
        while(b>a&&isspace((unsigned char)low[b-1])) --b;
        low=low.substr(a,b-a);
        std::transform(low.begin(),low.end(),low.begin(),::tolower);
        if(low!="save"&&low!="load"&&low!="quit") return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// GameResult
// ─────────────────────────────────────────────────────────────
struct GameResult {
    enum class Kind { None, Str, End } kind = Kind::None;
    std::string str;
    EndGame eg;

    static GameResult fromStr(const std::string& s)   { GameResult r; r.kind=Kind::Str; r.str=s; return r; }
    static GameResult fromEnd(const std::string& d, const std::string& m, bool win)
        { GameResult r; r.kind=Kind::End; r.eg={d,m,win}; return r; }
    bool isStr() const { return kind==Kind::Str; }
    bool isEnd() const { return kind==Kind::End; }
    const std::string& asStr() const { return str; }
};
static GameResult gStr(const std::string& s) { return GameResult::fromStr(s); }
static GameResult gEnd(const std::string& d, const std::string& m, bool win) { return GameResult::fromEnd(d,m,win); }

// ─────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────
class GameObject;
class WorldPosition;
class WorldBase;
class Person;
class Path;
class InsideWorld;
class NPC;
class LockedBox;

// ─────────────────────────────────────────────────────────────
// Container — anything that can hold GameObjects
// ─────────────────────────────────────────────────────────────
class Container {
public:
    std::set<GameObject*> holding;
    Props props; // "type", "objectsare", etc.  (distinct name avoids all ambiguity)
    virtual void add(GameObject* o)    { holding.insert(o); }
    virtual void remove(GameObject* o) { holding.erase(o); }
    // Typed downcast via virtual dispatch (no RTTI needed).
    virtual WorldPosition* asWorldPosition() { return nullptr; }
    virtual ~Container() = default;
};

// ─────────────────────────────────────────────────────────────
// WorldBase
// ─────────────────────────────────────────────────────────────
class WorldBase {
public:
    std::pair<int,int> size;
    std::set<Path*> paths;
    Props wprops; // "type"
    std::map<std::pair<int,int>, WorldPosition*> positions;
    WorldPosition* exitposition = nullptr;
    WorldPosition* startingpos = nullptr;
    virtual InsideWorld* asInsideWorld() { return nullptr; }
    virtual ~WorldBase() = default;
};

// ─────────────────────────────────────────────────────────────
// WorldPosition
// ─────────────────────────────────────────────────────────────
class WorldPosition : public Container {
public:
    WorldPosition* asWorldPosition() override { return this; }
    WorldBase* world;
    std::pair<int,int> worldposition;
    Props properties;           // "type","inside","insidereference"
    GameObject* blocking = nullptr;
    GameObject* guarding = nullptr;
    GameObject* insideworld = nullptr;

    // points: dir_string → { highlightname → (thing, pluralword) }, pluralword
    // empty means singular (use a/an); non-empty replaces the article (like
    // an object's pluralreference, e.g. "some").
    using PointMap = std::map<std::string, std::pair<std::string,std::string>>;
    std::map<std::string, PointMap> points;
    std::unordered_set<std::string> skipSet;

    WorldPosition(WorldBase* w, std::pair<int,int> pos) : world(w), worldposition(pos) {
        properties["type"] = std::string("world-position");
        props["type"]      = std::string("world-position"); // Container::props
        for (const auto& dir : std::vector<std::string>{
                "","north","south","east","west",
                "north-east","north-west","south-east","south-west"}) {
            std::string key = dir.empty() ? "Right next to me" : "On my "+dir;
            points[key] = {};
        }
    }
    void blockObj(GameObject* o) { blocking = o; }
    void unblock()               { blocking = nullptr; }
    void guardObj(GameObject* o) { guarding = o; }
    void unguard()               { guarding = nullptr; }
    void addinworld(GameObject* o) { insideworld = o; }
    void removeinworld()           { insideworld = nullptr; }
    void insideSet(const std::string& place, const std::string& ref, const std::string& pluralref="") {
        properties["inside"]          = place;
        properties["insidereference"] = ref;
        if(!pluralref.empty()) properties["pluralreference"] = pluralref;
    }
    void point(const std::string& name, const std::string& thing, const std::string& pluralword,
               const std::string& dir, const std::string& skip="") {
        std::string key = dir.empty() ? "Right next to me" : "On my "+dir;
        points[key][name] = {thing, pluralword};
        skipSet.insert(skip.empty() ? name : skip);
    }
};

// ─────────────────────────────────────────────────────────────
// World
// ─────────────────────────────────────────────────────────────
class World : public WorldBase {
public:
    explicit World(std::pair<int,int> sz) {
        size = sz;
        wprops["type"] = std::string("world");
        for (int x=0;x<size.first;++x)
            for (int y=0;y<size.second;++y)
                positions[{x,y}] = new WorldPosition(this,{x,y});
    }
    void pointall(std::pair<int,int> pos, const std::string& name, const std::string& thing, const std::string& pluralword, const std::string& skip="") {
        static const std::map<std::pair<int,int>,std::string> dirnames = {
            {{0,1},"south"},{{0,-1},"north"},{{1,0},"west"},{{-1,0},"east"},
            {{1,1},"south-west"},{{-1,1},"south-east"},{{1,-1},"north-west"},{{-1,-1},"north-east"}
        };
        for (int xi=-1; xi<=1; ++xi) for (int yi=-1; yi<=1; ++yi) {
            std::pair<int,int> np={pos.first+xi,pos.second+yi};
            if(!positions.count(np)) continue;
            auto it=dirnames.find({xi,yi});
            std::string dir_ = it!=dirnames.end()?it->second:"";
            positions.at(np)->point(name,thing,pluralword,dir_,skip);
        }
    }
    ~World() { for (auto& kv:positions) delete kv.second; }
    void inside(std::pair<int,int> pos, const std::string& place, const std::string& ref, const std::string& pluralref="") {
        positions.at(pos)->insideSet(place, ref, pluralref);
    }
};

// ─────────────────────────────────────────────────────────────
// Dialogue tree
// ─────────────────────────────────────────────────────────────
struct Dialogue;
// Insertion-order-preserving option map (mirrors Python dict ordering)
struct OrderedOptions {
    std::vector<std::pair<std::string, std::shared_ptr<Dialogue>>> items;
    std::shared_ptr<Dialogue>& operator[](const std::string& k) {
        for(auto& kv:items) if(kv.first==k) return kv.second;
        items.emplace_back(k, nullptr); return items.back().second;
    }
    std::shared_ptr<Dialogue>& at(const std::string& k) { return (*this)[k]; }
    bool empty() const { return items.empty(); }
    auto begin() { return items.begin(); }
    auto end()   { return items.end(); }
    auto begin() const { return items.begin(); }
    auto end()   const { return items.end(); }
};
struct Dialogue {
    std::string text;
    OrderedOptions options; // empty = terminal
    std::vector<GameObject*> giveItems;
    std::optional<EndGame> endgame;
};
using DPtr = std::shared_ptr<Dialogue>;

static DPtr term(const std::string& t) {
    auto d=std::make_shared<Dialogue>(); d->text=t; return d;
}
static DPtr termEnd(const EndGame& eg) {
    auto d=std::make_shared<Dialogue>(); d->endgame=eg; return d;
}
static DPtr giveD(const std::string& t, std::vector<GameObject*> items) {
    auto d=std::make_shared<Dialogue>(); d->text=t; d->giveItems=std::move(items); return d;
}

// ─────────────────────────────────────────────────────────────
// GameObject
// ─────────────────────────────────────────────────────────────
class GameObject {
public:
    Container* position;
    Props properties;

    explicit GameObject(Container* pos) : position(pos) { position->add(this); }
    virtual ~GameObject() = default;

protected:
    // Constructor for embedded objects (Lights, Button) that add themselves manually
    struct NoAdd {};
    explicit GameObject(NoAdd, Container* pos) : position(pos) {}
public:

    virtual void moveToContainer(Container* np) {
        position->remove(this); position=np; np->add(this);
    }
    virtual void deleteObj() { position->remove(this); }

    virtual GameResult use(Person*, GameObject* = nullptr) { return gStr("I can't do that."); }
    virtual GameResult press(Person*)  { return gStr("That's ridiculous."); }
    virtual GameResult dig(Person*, GameObject* /*tool*/=nullptr) { return gStr("That's ridiculous."); }
    virtual GameResult wear(Person*)   { return gStr("That's ridiculous."); }
    virtual GameResult unwear(Person*) { return gStr("That's ridiculous."); }
    virtual GameResult openContainer(Person*)  { return gStr("That's ridiculous."); }
    virtual GameResult closeContainer(Person*) { return gStr("That's ridiculous."); }
    virtual std::string read() { return ""; }
    virtual void write(const std::string&) {}

    virtual DPtr dialogues(Person*) { return nullptr; }
    virtual std::pair<bool,DPtr> guardtalk(Person*) { return {true,nullptr}; }
    virtual std::pair<bool,DPtr> give(GameObject*, Person*) { return {false,nullptr}; }

    // Typed downcasts via virtual dispatch (no RTTI needed).
    // holdingPtr() returns this object's item set if it is a container, else nullptr.
    virtual std::set<GameObject*>* holdingPtr() { return nullptr; }
    virtual NPC*         asNPC()         { return nullptr; }
    virtual LockedBox*   asLockedBox()   { return nullptr; }
    virtual InsideWorld* asInsideWorld() { return nullptr; }
};

// ─────────────────────────────────────────────────────────────
// Inventory
// ─────────────────────────────────────────────────────────────
class Inventory : public Container {
public:
    Inventory() { props["type"] = std::string("inventory"); }
};

// ─────────────────────────────────────────────────────────────
// Person
// ─────────────────────────────────────────────────────────────
class Person {
public:
    WorldPosition* position;
    Inventory inventory;
    Props properties;
    std::set<GameObject*> wearing;

    explicit Person(WorldPosition* pos) : position(pos) {
        properties["type"]    = std::string("person");
        properties["object"]  = std::string("person");
        properties["movable"] = true;
        // Insert a sentinel so the engine can find "person" in holding
        position->holding.insert(reinterpret_cast<GameObject*>(this));
    }
    void wear(GameObject* o)   { wearing.insert(o); }
    void unwear(GameObject* o) { wearing.erase(o); }
    void move(WorldPosition* np) {
        position->holding.erase(reinterpret_cast<GameObject*>(this));
        position = np;
        np->holding.insert(reinterpret_cast<GameObject*>(this));
    }
    void move(Container* np) {
        if (auto* wp = position->asWorldPosition())
            wp->holding.erase(reinterpret_cast<GameObject*>(this));
        position = np->asWorldPosition();
        if (position)
            position->holding.insert(reinterpret_cast<GameObject*>(this));
    }
    void take(GameObject* obj) {
        if (propBool(obj->properties,"movable"))
            obj->moveToContainer(&inventory);
    }
    void drop(GameObject* obj, Container* dest=nullptr) {
        if (!dest) dest = position;
        if (inventory.holding.count(obj))
            obj->moveToContainer(dest);
    }
};

// ─────────────────────────────────────────────────────────────
// ContainerObject — a GameObject that also holds items.
//   Item access goes through the holdingPtr() virtual.
// ─────────────────────────────────────────────────────────────
class ContainerObject : public GameObject, public Container {
public:
    std::set<GameObject*> holding;

    explicit ContainerObject(Container* pos) : GameObject(pos) {
        properties["type"] = std::string("container");
    }
    std::set<GameObject*>* holdingPtr() override { return &holding; }
    void add(GameObject* o)    override { holding.insert(o); }
    void remove(GameObject* o) override { holding.erase(o); }
    void moveToContainer(Container* np) override {
        position->remove(this); position=np; np->add(this);
    }
    void deleteObj() override { position->remove(this); }
};

// ─────────────────────────────────────────────────────────────
// BlockingObject
// ─────────────────────────────────────────────────────────────
class BlockingObject : public GameObject {
public:
    explicit BlockingObject(Container* pos) : GameObject(pos) {
        properties["type"] = std::string("blocking-object");
        if (auto* wp=pos->asWorldPosition()) wp->blockObj(this);
    }
    void moveToContainer(Container* np) override {
        if (auto* wp=position->asWorldPosition()) wp->unblock();
        position->remove(this); position=np; np->add(this);
        if (auto* wp=np->asWorldPosition()) wp->blockObj(this);
    }
    void deleteObj() override {
        if (auto* wp=position->asWorldPosition()) wp->unblock();
        position->remove(this);
    }
};

// ─────────────────────────────────────────────────────────────
// BlockingContainerObject
// ─────────────────────────────────────────────────────────────
class BlockingContainerObject : public GameObject, public Container {
public:
    std::set<GameObject*> holding;
    explicit BlockingContainerObject(Container* pos) : GameObject(pos) {
        properties["type"] = std::string("blocking-container");
        if (auto* wp=pos->asWorldPosition()) wp->blockObj(this);
    }
    std::set<GameObject*>* holdingPtr() override { return &holding; }
    void add(GameObject* o)    override { holding.insert(o); }
    void remove(GameObject* o) override { holding.erase(o); }
    void moveToContainer(Container* np) override {
        if (auto* wp=position->asWorldPosition()) wp->unblock();
        position->remove(this); position=np; np->add(this);
        if (auto* wp=np->asWorldPosition()) wp->blockObj(this);
    }
    void deleteObj() override {
        if (auto* wp=position->asWorldPosition()) wp->unblock();
        position->remove(this);
    }
};

// ─────────────────────────────────────────────────────────────
// NPC
// ─────────────────────────────────────────────────────────────
class NPC : public GameObject {
public:
    NPC* asNPC() override { return this; }
    explicit NPC(Container* pos) : GameObject(pos) {
        properties["type"]    = std::string("npc");
        properties["movable"] = false;
    }
    void moveToContainer(Container* np) override {
        position->remove(this); position=np; np->add(this);
    }
    void deleteObj() override { position->remove(this); }
};

// ─────────────────────────────────────────────────────────────
// Note
// ─────────────────────────────────────────────────────────────
class Note : public GameObject {
public:
    std::string text;
    Note(Container* pos, const std::string& t) : GameObject(pos), text(t) {
        properties["type"] = std::string("note");
    }
    std::string read() override { return text; }
    void write(const std::string& t) override { text=t; }
    void moveToContainer(Container* np) override { position->remove(this); position=np; np->add(this); }
    void deleteObj() override { position->remove(this); }
};

// ─────────────────────────────────────────────────────────────
// Path
// ─────────────────────────────────────────────────────────────
class Path : public GameObject {
public:
    WorldBase* world;
    std::pair<int,int> a, b;
    std::pair<std::string,std::string> direction; // (a→b, b→a)

    Path(WorldBase* w, std::pair<int,int> a_, std::pair<int,int> b_)
        : GameObject(w->positions.at(a_)), world(w), a(a_), b(b_)
    {
        position->remove(this); // paths aren't in a WorldPosition's holding
        properties["type"]    = std::string("path");
        properties["movable"] = false;
        static const std::map<std::pair<int,int>,std::pair<std::string,std::string>> dm = {
            {{0,1},{"south","north"}},{{0,-1},{"north","south"}},
            {{-1,0},{"east","west"}},{{1,0},{"west","east"}},
            {{-1,-1},{"north-east","south-west"}},{{1,1},{"south-west","north-east"}},
            {{-1,1},{"south-east","north-west"}},{{1,-1},{"north-west","south-east"}}
        };
        int dx = b.first==a.first?0:(b.first>a.first?1:-1);
        int dy = b.second==a.second?0:(b.second>a.second?1:-1);
        direction = dm.at({dx,dy});
        world->paths.insert(this);
    }
};

class StartingPath : public Path {
public:
    StartingPath(WorldBase* w, std::pair<int,int> a_, std::pair<int,int> b_) : Path(w,a_,b_) {
        properties["object"]         = std::string("dirt trail");
        properties["color"]         = std::string("brown");
        properties["other"]          = std::string("narrow,");
        properties["secondname"]     = std::string("trail");
        properties["insidereference"]= std::string("on");
    }
};

// ─────────────────────────────────────────────────────────────
// InsideWorld
// ─────────────────────────────────────────────────────────────
class InsideWorld : public WorldBase, public GameObject {
public:
    InsideWorld* asInsideWorld() override { return this; }
    std::set<std::pair<int,int>> exitablepositions;
    std::set<std::pair<int,int>> exitfrom;
    // exitablePos: an explicit set, or the sentinel "all"/"visible" (mirrors
    // Python's exitablepositions=='all'/'visible' string sentinels).
    InsideWorld(Container* outerPos, std::pair<int,int> sz,
                WorldPosition* exitPos, std::pair<int,int> startPos,
                std::variant<std::set<std::pair<int,int>>,std::string> exitablePos={},
                std::set<std::pair<int,int>> exitFrom={})
        : WorldBase(), GameObject(outerPos), exitfrom(exitFrom)
    {
        size = sz;
        wprops["type"]     = std::string("inside-world");
        properties["type"] = std::string("inside-world");
        for (int x=0;x<size.first;++x)
            for (int y=0;y<size.second;++y)
                positions[{x,y}] = new WorldPosition(this,{x,y});
        if(auto* tag=std::get_if<std::string>(&exitablePos)) {
            if(*tag=="all") {
                for(auto& kv:positions) exitablepositions.insert(kv.first);
            } else if(*tag=="visible") {
                for(auto& ep:exitfrom)
                    for(int x=ep.first-1;x<ep.first+2;++x)
                        for(int y=ep.second-1;y<ep.second+2;++y)
                            if(positions.count({x,y})) exitablepositions.insert({x,y});
            }
        } else {
            exitablepositions = std::get<std::set<std::pair<int,int>>>(exitablePos);
        }
        startingpos  = positions.at(startPos);
        exitposition = exitPos;
        if (auto* wp=outerPos->asWorldPosition()) wp->addinworld(this);
    }
    ~InsideWorld() { for (auto& kv:positions) delete kv.second; }

    void insideSet(std::pair<int,int> pos, const std::string& place, const std::string& ref, const std::string& pluralref="") {
        positions.at(pos)->insideSet(place, ref, pluralref);
    }
    void pointall(std::pair<int,int> pos, const std::string& name, const std::string& thing, const std::string& pluralword, const std::string& skip="") {
        static const std::map<std::pair<int,int>,std::string> dirnames = {
            {{0,1},"south"},{{0,-1},"north"},{{1,0},"west"},{{-1,0},"east"},
            {{1,1},"south-west"},{{-1,1},"south-east"},{{1,-1},"north-west"},{{-1,-1},"north-east"}
        };
        for (int xi=-1; xi<=1; ++xi) for (int yi=-1; yi<=1; ++yi) {
            std::pair<int,int> np={pos.first+xi,pos.second+yi};
            if(!positions.count(np)) continue;
            auto it=dirnames.find({xi,yi});
            std::string dir_ = it!=dirnames.end()?it->second:"";
            positions.at(np)->point(name,thing,pluralword,dir_,skip);
        }
    }
    void moveToContainer(Container* np) override {
        if (auto* wp=position->asWorldPosition()) wp->removeinworld();
        position->remove(this); position=np; np->add(this);
        if (auto* wp=np->asWorldPosition()) wp->addinworld(this);
    }
    void deleteObj() override {
        if (auto* wp=position->asWorldPosition()) wp->removeinworld();
        position->remove(this);
    }
};

// ─────────────────────────────────────────────────────────────
// Concrete objects
// ─────────────────────────────────────────────────────────────
#define SIMPLE_MOVE_DEL \
    void moveToContainer(Container* np) override { position->remove(this); position=np; np->add(this); } \
    void deleteObj() override { position->remove(this); }

class Apple : public GameObject {
public:
    explicit Apple(Container* pos) : GameObject(pos) {
        properties["movable"]=true; properties["object"]=std::string("apple");
        properties["color"]=std::string("red"); properties["other"]=std::string("shiny");
    } SIMPLE_MOVE_DEL
};
class Banana : public GameObject {
public:
    explicit Banana(Container* pos) : GameObject(pos) {
        properties["movable"]=true; properties["object"]=std::string("banana");
        properties["color"]=std::string("yellow"); properties["other"]=std::string("soft, tasty-looking");
    } SIMPLE_MOVE_DEL
};
class Coin : public GameObject {
public:
    explicit Coin(Container* pos) : GameObject(pos) {
        properties["movable"]=true; properties["object"]=std::string("coin");
        properties["color"]=std::string("golden"); properties["material"]=std::string("solid gold");
        properties["other"]=std::string("shiny");
    } SIMPLE_MOVE_DEL
};

class Table : public ContainerObject {
public:
    explicit Table(Container* pos) : ContainerObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("table");
        properties["color"]=std::string("brown"); properties["material"]=std::string("wood");
        properties["height"]=1; properties["objectsare"]=std::string("on");
        properties["message"]=std::string("The table looks too heavy to move.");
    }
};

class LockedBox : public ContainerObject {
public:
    LockedBox* asLockedBox() override { return this; }
    std::string closedname="locked box", openname="open box";
    explicit LockedBox(Container* pos) : ContainerObject(pos) {
        lock();
        properties["movable"]=true; properties["color"]=std::string("metal-colored");
        properties["material"]=std::string("iron"); properties["objectsare"]=std::string("in");
        properties["other"]=std::string("strong"); properties["secondname"]=std::string("box");
        properties["key"]=std::string("key");
    }
    void lock()   { properties["open"]=false; properties["object"]=closedname; }
    void unlock() { properties["open"]=true;  properties["object"]=openname; }
};

class Key : public GameObject {
public:
    Key(Container* pos, LockedBox*) : GameObject(pos) {
        properties["movable"]=true; properties["object"]=std::string("key");
        properties["color"]=std::string("faded black"); properties["material"]=std::string("iron");
        properties["other"]=std::string("old, heavy,"); properties["usable"]=true;
    }
    SIMPLE_MOVE_DEL
    GameResult use(Person*, GameObject* obj=nullptr) override {
        if (!obj) return gStr("What should I use it on?");
        if (!propBool(obj->properties,"open")) { obj->asLockedBox()->unlock(); return gStr("The box\x1b[0m opens with a click."); }
        obj->asLockedBox()->lock(); return gStr("I close and lock the box\x1b[0m.");
    }
};

class InvisibilityCloak : public GameObject {
public:
    explicit InvisibilityCloak(Container* pos) : GameObject(pos) {
        properties["movable"]=true; properties["object"]=std::string("cloak");
        properties["color"]=std::string("silver"); properties["material"]=std::string("a very soft material");
        properties["other"]=std::string("shimmering"); properties["usable"]=true;
        properties["wearable"]=true;
    } SIMPLE_MOVE_DEL
    GameResult use(Person* p, GameObject* =nullptr) override { return wear(p); }
    GameResult wear(Person* p) override {
        p->wear(this); p->properties["invisible"]=true; return gStr("I wear the cloak.");
    }
    GameResult unwear(Person* p) override {
        p->unwear(this); p->properties["invisible"]=false; return gStr("I remove the cloak.");
    }
};

class CloakChest : public ContainerObject {
public:
    explicit CloakChest(Container* pos) : ContainerObject(pos) {
        properties["movable"]=true; properties["object"]=std::string("chest");
        properties["color"]=std::string("golden"); properties["material"]=std::string("a light metal");
        properties["other"]=std::string("small"); properties["objectsare"]=std::string("in");
        properties["open"]=true;
        new InvisibilityCloak(this);
    }
};

class CodePaper : public Note {
public:
    static std::string numify(const std::string& text) {
        std::vector<std::string> parts;
        for(char c : text) {
            if(isdigit((unsigned char)c)) parts.push_back(std::string("num")+c);
            else parts.push_back("let"+std::to_string(std::tolower((unsigned char)c)-'a'+1));
        }
        std::string r; for(size_t i=0;i<parts.size();++i){ if(i) r+="-"; r+=parts[i]; }
        return r;
    }
    explicit CodePaper(Container* pos, const std::string& passwd) : Note(pos,
        "The text '"+numify(passwd)+"' is written on the paper.") {
        properties["movable"]=true; properties["object"]=std::string("piece of paper");
        properties["color"]=std::string("white"); properties["other"]=std::string("small");
        properties["secondname"]=std::string("paper");
    } SIMPLE_MOVE_DEL
};

class PasswdFolder : public ContainerObject {
public:
    explicit PasswdFolder(Container* pos) : ContainerObject(pos) {
        properties["movable"]=true; properties["object"]=std::string("folder");
        properties["color"]=std::string("white"); properties["open"]=false;
        properties["objectsare"]=std::string("inside");
    }
    GameResult openContainer(Person*) override { properties["open"]=true; return gStr("I open the folder."); }
    GameResult closeContainer(Person*) override { properties["open"]=false; return gStr("I close the folder."); }
};

class PasswdStickyNote : public Note {
public:
    PasswdStickyNote(Container* pos, const std::string& passwd) : Note(pos,
        "'"+passwd+"' is written on the sticky note.") {
        properties["movable"]=true; properties["object"]=std::string("sticky note");
        properties["color"]=std::string("yellow"); properties["other"]=std::string("small square");
    } SIMPLE_MOVE_DEL
};

class PasswordNote : public Note {
public:
    explicit PasswordNote(Container* pos, const std::string& passwd) : Note(pos,
        "The note shows the text '"+passwd+"'.") {
        properties["movable"]=true; properties["object"]=std::string("note");
        properties["other"]=std::string("dusty old");
    } SIMPLE_MOVE_DEL
};

class Archway : public GameObject {
public:
    explicit Archway(Container* pos) : GameObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("archway");
        properties["material"]=std::string("concrete"); properties["height"]=3;
        properties["other"]=std::string("large");
    } SIMPLE_MOVE_DEL
};

class StartingDitch : public BlockingObject {
public:
    StartingDitch(Container* pos, int width) : BlockingObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("ditch");
        properties["color"]=std::string("brown"); properties["other"]=std::string("dirty");
        properties["width"]=width; properties["message"]=std::string("It is too wide to jump across.");
    }
};
class NormalWall : public BlockingObject {
public:
    NormalWall(Container* pos, int height) : BlockingObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("wall");
        properties["color"]=std::string("white"); properties["material"]=std::string("stone");
        properties["height"]=height;
        if(height>3) properties["other"]=std::string("tall");
    }
};
class ContainerWall : public BlockingContainerObject {
public:
    ContainerWall(Container* pos, int height) : BlockingContainerObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("wall");
        properties["color"]=std::string("white"); properties["material"]=std::string("stone");
        properties["height"]=height;
        properties["objectsare"]=std::string("wedged between some stone blocks in");
        properties["hiddeninside"]=true; properties["putinside"]=false;
        if(height>3) properties["other"]=std::string("tall");
    }
};

class Sheet : public GameObject {
public:
    explicit Sheet(Container* pos) : GameObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("sheet");
        properties["other"]=std::string("colorful peppa-pig");
    } SIMPLE_MOVE_DEL
};
class Bed : public ContainerObject {
public:
    explicit Bed(Container* pos) : ContainerObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("bed");
        properties["other"]=std::string("single"); properties["objectsare"]=std::string("on");
        properties["height"]=0.3;
        new Sheet(this);
    }
};
class Book : public Note {
public:
    Book(Container* pos, const std::string& text) : Note(pos,text) {
        properties["movable"]=true; properties["object"]=std::string("book");
        properties["other"]=std::string("black hardcover");
        properties["secondname"]=std::string("diary");
    } SIMPLE_MOVE_DEL
};
class Desk : public ContainerObject {
public:
    explicit Desk(Container* pos) : ContainerObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("desk");
        properties["other"]=std::string("low"); properties["material"]=std::string("wood");
        properties["objectsare"]=std::string("on");
        new Book(this,
            "It seems to be a diary.\nI go through the pages... and find something!\n\n...\n"
            "Today some strange people came here with a device and hid it somewhere on the island.\n"
            "I heard them talking about keeping some \"passwords\" hidden so they could find and open the device themselves.\n"
            "I heard one of the passwords!\nI wonder what it's for... \""+PASSWORDS[1]+"\" it was.\n...");
    }
};
class Platform : public ContainerObject {
public:
    explicit Platform(Container* pos) : ContainerObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("platform");
        properties["other"]=std::string("low"); properties["material"]=std::string("marble");
    }
};
class BedRoom : public InsideWorld {
public:
    BedRoom(WorldPosition* outer, WorldPosition* exitPos, std::pair<int,int> startPos={0,0})
        : InsideWorld(outer,{1,2},exitPos,startPos)
    {
        for (auto& kv:positions) kv.second->insideSet("bedroom inside a house","in");
        properties["movable"]=false; properties["object"]=std::string("bedroom");
        properties["insidereference"]=std::string("inside");
        new Bed(positions.at({0,0}));
        new Desk(positions.at({0,1}));
    }
};
class Kitchen : public InsideWorld {
public:
    Kitchen(WorldPosition* outer, WorldPosition* exitPos, std::pair<int,int> startPos={0,0})
        : InsideWorld(outer,{1,2},exitPos,startPos)
    {
        for (auto& kv:positions) kv.second->insideSet("kitchen inside a house","in");
        properties["movable"]=false; properties["object"]=std::string("kitchen");
        properties["insidereference"]=std::string("inside");
        new Platform(positions.at({0,1}));
    }
};
class House : public InsideWorld {
public:
    House(WorldPosition* outer, WorldPosition* exitPos)
        : InsideWorld(outer,{2,2},exitPos,{1,0})
    {
        for (auto& kv:positions) kv.second->insideSet("house","inside");
        properties["movable"]=false; properties["object"]=std::string("house");
        properties["color"]=std::string("red"); properties["material"]=std::string("wooden planks");
        properties["other"]=std::string("small"); properties["insidereference"]=std::string("inside");
        new BedRoom(positions.at({1,1}), positions.at({1,0}));
        new Kitchen(positions.at({0,0}), positions.at({1,0}));
    }
};

class ShipHallway : public Path {
public:
    ShipHallway(WorldBase* w, std::pair<int,int> a_, std::pair<int,int> b_) : Path(w,a_,b_) {
        properties["object"]=std::string("long hallway");
        properties["secondname"]=std::string("hallway");
        properties["insidereference"]=std::string("in");
    }
};
class ShipNote : public Note {
public:
    ShipNote(Container* pos, const std::string& text) : Note(pos,text) {
        properties["movable"]=true; properties["object"]=std::string("card");
        properties["color"]=std::string("white"); properties["material"]=std::string("a thin paper");
        properties["other"]=std::string("folded");
        properties["message"]=std::string("Something is written on the card.");
    } SIMPLE_MOVE_DEL
};
class ShipTable : public ContainerObject {
public:
    explicit ShipTable(Container* pos) : ContainerObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("table");
        properties["other"]=std::string("low, wooden");
        properties["message"]=std::string("It looks like someone just got up from the table.");
        properties["objectsare"]=std::string("on");
    }
};
class ShipNoteRoom : public InsideWorld {
public:
    ShipNoteRoom(WorldPosition* outer, WorldPosition* exitPos)
        : InsideWorld(outer,{2,1},exitPos,{0,0})
    {
        for (auto& kv:positions) kv.second->insideSet("room inside a ship","in");
        properties["movable"]=false; properties["object"]=std::string("room");
        properties["other"]=std::string("small, dark"); properties["insidereference"]=std::string("in");
        auto* table=new ShipTable(positions.at({0,0}));
        new ShipNote(table,"It is a note:\n\nHello,\nI'm on the ship. I'll come back today night. Don't look for me.");
    }
};
class ShipGoodMan : public NPC {
public:
    bool talked=false;
    explicit ShipGoodMan(Container* pos) : NPC(pos) {
        properties["object"]=std::string("man"); properties["other"]=std::string("tall, cheerful looking");
        properties["otherafter"]=std::string("wearing a uniform"); properties["reference"]=std::string("he");
    }
    DPtr dialogues(Person* p) override {
        if(propBool(p->properties,"invisible"))
            return term("The man looks around, confused, almost like he can't see me.\n\nWhere are you?");
        if(talked) return term("Hello again!");
        talked=true;
        auto d=std::make_shared<Dialogue>(); d->text="Hello! Are you coming on the ship?";
        d->options["Yes"]=term("Bye then, see you later!");
        d->options["No"]=term("Oh, it's fine.");
        return d;
    }
    std::pair<bool,DPtr> give(GameObject* obj, Person* p) override {
        if(propBool(p->properties,"invisible"))
            return {false, term("The man looks around, confused, almost like he can't see me.\nWhere are you?")};
        std::string on=propStr(obj->properties,"object");
        if(on=="folder"||on=="sticky note") {
            auto d=std::make_shared<Dialogue>(); d->text="Where did you get this?";
            d->options["An old lady gave it to me"]=term("Well, I already took another copy.");
            return {false,d};
        }
        return {false,nullptr};
    }
};
class ShipManRoom : public InsideWorld {
public:
    ShipManRoom(WorldPosition* outer, WorldPosition* exitPos)
        : InsideWorld(outer,{1,1},exitPos,{0,0})
    {
        insideSet({0,0},"room inside a ship","in");
        properties["movable"]=false; properties["object"]=std::string("room");
        properties["other"]=std::string("large"); properties["insidereference"]=std::string("in");
        new ShipGoodMan(positions.at({0,0}));
    }
};
class ShipCabin : public InsideWorld {
public:
    ShipCabin(WorldPosition* outer, WorldPosition* exitPos)
        : InsideWorld(outer,{2,5},exitPos,{0,1})
    {
        for (auto& kv:positions) kv.second->insideSet("cabin of a ship","in","the");
        properties["movable"]=false; properties["object"]=std::string("cabin");
        properties["material"]=std::string("mostly of wood"); properties["other"]=std::string("small");
        properties["insidereference"]=std::string("in"); properties["materialnoof"]=true;
        new ShipHallway(this,{0,0},{0,3});
        new ShipManRoom(positions.at({1,2}), positions.at({0,2}));
        new ShipNoteRoom(positions.at({0,4}), positions.at({0,3}));
    }
};
class Ship : public InsideWorld {
public:
    Ship(WorldPosition* outer, WorldPosition* exitPos)
        : InsideWorld(outer,{3,1},exitPos,{1,0},{},{{0,0}})
    {
        for (auto& kv:positions) kv.second->insideSet("deck of a ship","on","the");
        properties["movable"]=false; properties["object"]=std::string("ship");
        properties["color"]=std::string("black"); properties["material"]=std::string("mostly of steel");
        properties["other"]=std::string("big"); properties["insidereference"]=std::string("in");
        properties["materialnoof"]=true;
        new ShipCabin(positions.at({2,0}), positions.at({1,0}));
    }
};

class Spade : public GameObject {
public:
    explicit Spade(Container* pos) : GameObject(pos) {
        properties["movable"]=true; properties["object"]=std::string("spade");
        properties["other"]=std::string("sharp"); properties["usable"]=true;
    } SIMPLE_MOVE_DEL
    GameResult use(Person* p, GameObject* obj=nullptr) override {
        if (!obj) return gStr("What should I use it on?");
        if (propBool(obj->properties,"digable")) return obj->dig(p,this);
        return gStr("That's ridiculous.");
    }
};

class BabyFood : public GameObject {
public:
    explicit BabyFood(Container* pos) : GameObject(pos) {
        properties["movable"]=true; properties["object"]=std::string("baby food");
        properties["other"]=std::string("mushy"); properties["secondname"]=std::string("food");
        properties["pluralreference"]=std::string("some");
    } SIMPLE_MOVE_DEL
};

class Trees : public GameObject {
public:
    explicit Trees(Container* pos) : GameObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("trees");
        properties["other"]=std::string("thick, tall"); properties["plural"]=true;
        properties["pluralreference"]=std::string("some"); properties["reference"]=std::string("they");
    } SIMPLE_MOVE_DEL
};

class Stream : public GameObject {
public:
    explicit Stream(Container* pos) : GameObject(pos) {
        properties["movable"]=false; properties["object"]=std::string("stream");
        properties["other"]=std::string("narrow");
    } SIMPLE_MOVE_DEL
};

class ForestHut : public InsideWorld {
public:
    ForestHut(WorldPosition* outer, WorldPosition* exitPos)
        : InsideWorld(outer,{1,1},exitPos,{0,0})
    {
        for (auto& kv:positions) kv.second->insideSet("hut","inside");
        properties["movable"]=false; properties["object"]=std::string("hut");
        properties["other"]=std::string("small"); properties["insidereference"]=std::string("in");
        new BabyFood(new Table(positions.at({0,0})));
    }
};

class ForestBranch : public Note {
public:
    explicit ForestBranch(Container* pos) : Note(pos,"The text is scratched on the fallen branch:\n"+LAST_PASSWD_CODE) {
        properties["movable"]=false; properties["object"]=std::string("fallen branch");
        properties["color"]=std::string("brown"); properties["other"]=std::string("medium-sized");
        properties["secondname"]=std::string("branch");
        properties["message"]=std::string("There are some faint scratches on the branch.");
    } SIMPLE_MOVE_DEL
};

class StartingForest : public InsideWorld {
public:
    StartingForest(WorldPosition* outer, WorldPosition* exitPos)
        : InsideWorld(outer,{5,5},exitPos,{2,3},std::string("visible"),{{2,4}})
    {
        pointall({2,4},"exit","to the forest","");
        for (auto& kv:positions) kv.second->insideSet("forest","in");
        properties["movable"]=false; properties["object"]=std::string("forest");
        properties["other"]=std::string("big, dense"); properties["insidereference"]=std::string("in");
        new Trees(positions.at({1,3}));
        new Stream(positions.at({2,2}));
        new Spade(positions.at({2,1}));
        new ForestBranch(positions.at({2,0}));
        new ForestHut(positions.at({3,3}), positions.at({2,3}));
    }
};

// Lights & Button forward-declare MainDevice
class Lights : public GameObject {
public:
    int total, on=0;
    explicit Lights(ContainerObject* dev) : GameObject(NoAdd{}, dev), total((int)PASSWORDS.size()) {
        dev->holding.insert(this);
        properties["movable"]=false; properties["object"]=std::string("lights");
        properties["plural"]=true; properties["color"]=std::string("green");
        properties["reference"]=std::string("they"); properties["pluralreference"]=std::string("some");
        properties["secondname"]=std::string("light");
        properties["message"]="None of the lights are on out of "+std::to_string(total)+" total lights.";
    }
    void moveToContainer(Container*) override { /* lights stay fixed */ }
    void deleteObj() override {}
    void turnon(int n) {
        on+=n;
        properties["message"]=std::to_string(on)+" "+(on!=1?"\x1b[1m\x1b[38;5;136mlights\x1b[0m are":"\x1b[1m\x1b[38;5;136mlight\x1b[0m is")+" on out of "+std::to_string(total)+" total \x1b[1m\x1b[38;5;136mlights\x1b[0m.";
    }
};

class Button : public GameObject {
public:
    std::unordered_set<std::string> entered;
    Lights* lights;
    Button(ContainerObject* dev, Lights* l) : GameObject(NoAdd{}, dev), lights(l)
    {
        dev->holding.insert(this);
        properties["movable"]=false; properties["object"]=std::string("button");
        properties["color"]=std::string("red");
        properties["message"]=std::string("I don't think you should press it.");
        properties["type"]=std::string("button"); properties["usable"]=true;
    }
    void moveToContainer(Container*) override {}
    void deleteObj() override {}
    GameResult use(Person* p, GameObject* =nullptr) override { return press(p); }
    GameResult press(Person*) override {
        std::string code=getInputWrapped("code: ");
        while(!code.empty()&&(code.back()=='\r'||code.back()==' ')) code.pop_back();
        std::transform(code.begin(),code.end(),code.begin(),::tolower);
        pyprint("\n");
        bool valid=std::find(PASSWORDS.begin(),PASSWORDS.end(),code)!=PASSWORDS.end();
        if(entered.count(code)||!valid)
            return gEnd("The device explodes!\nYou now have no way of recovering your work.","You should have been more careful.",false);
        entered.insert(code);
        if(entered.size()==PASSWORDS.size())
            return gEnd("Another light turns on in the device.\nAll the lights are now on!\nThe device opens to show the result of your experiment... \x1b[0m\x1b[1;40;36m 42 ","You have recovered your hard work!",true);
        lights->turnon(1);
        return gStr(entered.size()!=1?"Another light turns on in the device!":"A light turns on in the device!");
    }
};

class MainDevice : public ContainerObject {
public:
    Lights* lights;
    Button* button;
    explicit MainDevice(Container* pos) : ContainerObject(pos) {
        lights = new Lights(this);
        button = new Button(this, lights);
        properties["movable"]=true; properties["object"]=std::string("electronic device");
        properties["objectsare"]=std::string("on"); properties["message"]=std::string("The device looks very important.");
        properties["secondname"]=std::string("device"); properties["putinside"]=false; properties["usable"]=true;
    }
    GameResult use(Person* p, GameObject* =nullptr) override { return button->press(p); }
};

class SandPatch : public ContainerObject {
public:
    int diglevel=0;
    std::function<GameObject*(ContainerObject*)> factory;
    std::string uncvrmsg;
    SandPatch(Container* pos, std::function<GameObject*(ContainerObject*)> f, const std::string& uncoverMsg)
        : ContainerObject(pos), factory(f), uncvrmsg(uncoverMsg)
    {
        properties["movable"]=false; properties["object"]=std::string("patch of sand");
        properties["color"]=std::string("white"); properties["other"]=std::string("small");
        properties["digable"]=true; properties["objectsare"]=std::string("on");
        properties["putinside"]=false; properties["secondname"]=std::string("sand");
        properties["digtool"]=std::string("spade");
        properties["nodigtoolmessage"]=std::string("I need a tool to do that...");
    }
    GameResult dig(Person*, GameObject* =nullptr) override {
        ++diglevel;
        if(diglevel==3) { factory(this); return gStr(uncvrmsg); }
        if(diglevel==1) return gStr("I make some progress.");
        if(diglevel==2) return gStr("I make some more progress.");
        return gStr("I can't dig any more.");
    }
};

// ─────────────────────────────────────────────────────────────
// NPCs
// ─────────────────────────────────────────────────────────────
class StartingHorse : public NPC {
public:
    WorldPosition* jumpposition;
    std::string returnmessage;
    bool applegiven=false;
    StartingHorse(Container* pos, WorldPosition* jump, const std::string& ret)
        : NPC(pos), jumpposition(jump), returnmessage(ret)
    {
        properties["object"]=std::string("horse"); properties["color"]=std::string("brown");
        properties["other"]=std::string("big"); properties["usable"]=true; properties["rideable"]=true;
    }
    DPtr dialogues(Person* p) override {
        if(propBool(p->properties,"invisible")) return term("The horse looks around, confused, almost like it can't see me.");
        return term("Harrumph!");
    }
    std::pair<bool,DPtr> give(GameObject* obj, Person*) override {
        if(propStr(obj->properties,"object")!="apple") return {false,nullptr};
        applegiven=true;
        return {true, term("Harrumph! The horse happily eats the apple.")};
    }
    GameResult use(Person* p, GameObject* /*obj*/=nullptr) override {
        if(p->position != position->asWorldPosition())
            return gStr("The horse is too far away.");
        if(propBool(p->properties,"invisible")) return gStr("The horse looks around, confused, almost like it can't see me.");
        if(!applegiven) return gStr("The horse throws me off.");
        auto* old = position->asWorldPosition();
        p->move(jumpposition);
        moveToContainer(jumpposition);
        jumpposition = old;
        return gStr(returnmessage);
    }
};

class Watchman : public NPC {
public:
    GameObject* guarded_obj;
    WorldPosition* outposition;
    Watchman(GameObject* obj, WorldPosition* outp) : NPC(obj->position), guarded_obj(obj), outposition(outp) {
        if(auto* wp=position->asWorldPosition()) wp->guardObj(this);
        properties["object"]=std::string("watchman"); properties["other"]=std::string("big, strong");
        properties["reference"]=std::string("he");
        properties["message"]="He is guarding the \x1b[1m\x1b[38;5;136m"+propStr(guarded_obj->properties,"object")+"\x1b[0m.";
    }
    DPtr dialogues(Person* p) override {
        if(propBool(p->properties,"invisible"))
            return term("The watchman looks around, confused, almost like he can't see me.\n\nWhere are you?");
        if(auto* wp=p->position->asWorldPosition())
            if(wp->worldposition.first>6) return term("What do you want? You can't go in.");
        p->move(outposition);
        return term("How did you get in?\n\nThe watchman shoves me back out.");
    }
    std::pair<bool,DPtr> guardtalk(Person* p) override {
        if(propBool(p->properties,"invisible")) return {true, term("I slip past the \x1b[1m\x1b[38;5;136mwatchman\x1b[0m, invisible.")};
        if(auto* wp=p->position->asWorldPosition())
            if(wp->worldposition.first>6) return {false, term("Where do you think you're going?")};
        p->move(outposition);
        return {false, term("How did you get in?\n\nThe watchman shoves me back out.")};
    }
    std::pair<bool,DPtr> give(GameObject*,Person* p) override {
        if(propBool(p->properties,"invisible"))
            return {false, term("The watchman looks around, confused, almost like he can't see me.\n\nWhere are you?")};
        return {false,nullptr};
    }
};

class OldLady : public NPC {
public:
    bool done=false;
    explicit OldLady(Container* pos) : NPC(pos) {
        properties["object"]=std::string("old lady"); properties["other"]=std::string("stern");
        properties["reference"]=std::string("she");
        properties["message"]=std::string("She is looking around for someone.");
        properties["secondname"]=std::string("lady");
    }
    DPtr dialogues(Person* p) override {
        if(propBool(p->properties,"invisible"))
            return term("The old lady looks around, confused, almost like she can't see me.\n\nWhere are you?");
        if(done) return term("Hello again.");
        auto d=std::make_shared<Dialogue>();
        d->text="Have you seen my son anywhere?";
        d->options["Yes, he's inside that house"] = termEnd(EndGame{
            "The old lady goes into the house with me.\n\n"
            "YOU DARE SUGGEST MY SON IS THIS... LUNATIC?\n\n"
            "With a sudden, powerful swing, the old lady's wooden stick connects with "
            "the side of my head. I stumble blindly, trying desperately to dodge, but I "
            "lose my footing and fall hard against the stones. The world spins, and everything goes black.",
            "A nice way to die, getting hit by an old lady.",false});
        d->options["No"] = term("Ok, tell me if you do, will you?");
        return d;
    }
    std::pair<bool,DPtr> give(GameObject* obj, Person* p) override {
        if(propBool(p->properties,"invisible"))
            return {false, term("The old lady looks around, confused, almost like she can't see me.\n\nWhere are you?")};
        if(propStr(obj->properties,"object")!="card") return {false,nullptr};
        auto* folder=new PasswdFolder(&p->inventory);
        new PasswdStickyNote(folder,PASSWORDS[3]);
        done=true;
        auto thankyou=giveD("Thank you!",{folder});
        auto okd=std::make_shared<Dialogue>(); okd->text="Oh no, he has forgotten this! Can you take it to him?"; okd->options["Ok!"]=thankyou;
        auto top=std::make_shared<Dialogue>();
        top->text="The old lady reads the note.\nOh! I have been looking around everywhere for him. So he's already on the ship.";
        top->options["Yes"]=okd;
        return {true,top};
    }
};

class JokeMan : public NPC {
public:
    std::vector<std::string> talklines;
    OldLady* oldlady=nullptr;
    WorldPosition* oldladyPos;
    int index=-1;
    JokeMan(Container* pos, const std::vector<std::string>& lines, WorldPosition* olp)
        : NPC(pos), talklines(lines), oldladyPos(olp)
    {
        properties["object"]=std::string("man"); properties["other"]=std::string("funny little");
        properties["reference"]=std::string("he");
        properties["message"]=std::string(
            "He is wearing a disturbing pink t-shirt with yellow polka dots on it.\nTry talking to him.");
    }
    DPtr dialogues(Person* p) override {
        if(propBool(p->properties,"invisible")) return term("Ooooh! Playing hide and seek, I like it! Where are you?");
        if(!oldlady) oldlady = new OldLady(oldladyPos);
        index = (index+1) % ((int)talklines.size()-1);
        return term(talklines[index]);
    }
    std::pair<bool,DPtr> give(GameObject* obj, Person* p) override {
        if(propBool(p->properties,"invisible"))
            return {false, term("Flying object in the air, oh flying object in the air!")};
        if(propStr(obj->properties,"object")=="baby food") {
            auto yes=giveD(
                "You know, once some people came to this island in a big flying thing!\n"
                "They gave me something and said don't show it or give it to anyone except for them.\n"
                "But because you gave me this amazing food, I will give it to you!",
                {new CodePaper(&p->inventory,PASSWORDS[2])});
            auto no=term("Waaaaaa I thought you were my friend go out of my house I'll not give it to you.");
            auto d=std::make_shared<Dialogue>(); d->text="Thank you!!! I love baby food.";
            d->options["Ok, no problem!"]=yes;
            d->options["Shut up and go away you unhinged little loser"]=no;
            return {true,d};
        }
        return {false,nullptr};
    }
};

class StartingMan : public NPC {
public:
    bool coingiven=false, done=false;
    explicit StartingMan(Container* pos) : NPC(pos) {
        properties["object"]=std::string("man"); properties["other"]=std::string("big, strong");
        properties["reference"]=std::string("he");
    }
    DPtr dialogues(Person* p) override {
        if(propBool(p->properties,"invisible")) return term("The man looks around, confused, almost like he can't see me.\n\nWhere are you?");
        if(done) return term("Thanks for the coin.");

        std::map<std::string,GameObject*> objs;
        for(auto* o:p->inventory.holding) objs[propStr(o->properties,"object")]=o;
        bool hasBox=objs.count("locked box")>0, hasCoin=objs.count("coin")>0;

        if(!hasBox) {
            if(coingiven) return term("Thanks for the coin. Come back if you ever need to open a lock.");
            auto inner=std::make_shared<Dialogue>();
            inner->text="I was once the greatest locksmith and metalworker in this place... What do you want?";
            inner->options["Nothing."]=term("Come back later then.");
            auto hello=std::make_shared<Dialogue>(); hello->text="Hello."; hello->options["Hi"]=inner;
            return hello;
        }
        if(coingiven) {
            auto* lb=objs["locked box"]->asLockedBox();
            done=true; auto* k=new Key(&p->inventory,lb);
            auto yes=giveD("Here is a key to open the box.",{k});
            auto d=std::make_shared<Dialogue>(); d->text="Do you want to open that box?"; d->options["Yes"]=yes;
            return d;
        }
        if(hasCoin) {
            objs["coin"]->deleteObj(); coingiven=true;
            auto* lb=objs["locked box"]->asLockedBox();
            done=true; auto* k=new Key(&p->inventory,lb);
            auto yes=giveD("Thanks. Here is a key for the box.",{k});
            auto want=std::make_shared<Dialogue>(); want->text="You want to open that box for a coin?"; want->options["Yes"]=yes;
            auto hi=std::make_shared<Dialogue>(); hi->text="Hello."; hi->options["Hi"]=want;
            return hi;
        }
        auto byeD=term("Well, bye then.");
        auto giveH=term("He looks at the box carefully and gives it back.\nI can if you have a coin...");
        auto showD=std::make_shared<Dialogue>(); showD->text="Show me the box";
        showD->options["Give him the box"]=giveH; showD->options["Don't give him the box"]=byeD;
        auto canOpen=std::make_shared<Dialogue>(); canOpen->text="What do you want?";
        canOpen->options["Can you open this box for me?"]=showD; canOpen->options["Nothing."]=term("Go away then.");
        auto hi=std::make_shared<Dialogue>(); hi->text="Hello."; hi->options["Hi"]=canOpen;
        return hi;
    }
    std::pair<bool,DPtr> give(GameObject* obj, Person* p) override {
        if(propBool(p->properties,"invisible"))
            return {false, term("The man looks around, confused, almost like he can't see me.\n\nWhere are you?")};
        if(done) return {false,nullptr};
        std::map<std::string,GameObject*> objs;
        for(auto* o:p->inventory.holding) objs[propStr(o->properties,"object")]=o;
        if(coingiven && propStr(obj->properties,"object")=="locked box") {
            done=true; auto* k=new Key(&p->inventory,obj->asLockedBox());
            auto yes=giveD("Here is a key to open the box.",{k});
            auto d=std::make_shared<Dialogue>(); d->text="Do you want to open that box?";
            d->options["Yes"]=yes;
            return {false,d};
        }
        if(objs.count("coin") && propStr(obj->properties,"object")=="locked box") {
            coingiven=true;
            objs["coin"]->deleteObj();
            done=true; auto* k=new Key(&p->inventory,obj->asLockedBox());
            auto yes=giveD("Here is a key to open the box.",{k});
            auto d=std::make_shared<Dialogue>(); d->text="Do you want to open that box for a coin?";
            d->options["Yes"]=yes;
            return {false,d};
        }
        if(propStr(obj->properties,"object")!="coin") return {false,nullptr};
        coingiven=true;
        DPtr dialog;
        if(objs.count("locked box")) {
            done=true; auto* k=new Key(&p->inventory,objs["locked box"]->asLockedBox());
            auto yes=giveD("Here is a key to open the box.",{k});
            auto d=std::make_shared<Dialogue>(); d->text="Thanks for the coin. Do you want to open that box?";
            d->options["Yes"]=yes; dialog=d;
        } else {
            auto ok=term("Go now.");
            auto d=std::make_shared<Dialogue>(); d->text="Thanks for the coin. If you ever want to open a lock, come to me.";
            d->options["Ok"]=ok; dialog=d;
        }
        return {true,dialog};
    }
};

static void cls() { pyprint("\x1b[H\x1b[2J\x1b[3J"); std::fflush(stdout); }

// ─────────────────────────────────────────────────────────────
// Engine
// ─────────────────────────────────────────────────────────────
class Engine {
public:
    WorldBase* world;
    Person* person;

    std::unordered_map<std::string,GameObject*> objectindex;
    struct AroundEntry { GameObject* obj; std::pair<int,int> dir; bool inCont=false; };
    std::unordered_map<std::string,AroundEntry> aroundobjects;
    std::unordered_map<std::string,GameObject*> inventoryobjects;

    struct PathEntry { Path* path; std::pair<int,int> a,b,pos; std::pair<std::string,std::string> dir; };
    std::unordered_map<std::string,PathEntry> aroundpaths;

    using CmdFn = std::function<GameResult(const std::string&)>;
    std::unordered_map<std::string,CmdFn> commands;
    std::unordered_set<std::string> ongoingcmds;

    int maxcommands=200;
    int donecommands=0;

    Engine() : world(nullptr), person(nullptr) {
        auto s=this;
        commands["help"]        =[s](const std::string& x){ return x.empty()?s->cmdHelp():gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); };
        commands["moves"]=commands["commands"]=[s](const std::string& x){ return x.empty()?gStr("\x1b[32mYou have "+std::to_string(s->maxcommands-s->donecommands)+" commands left.\x1b[0m"):gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); };
        commands["quit"]         =[s](const std::string& x){ return x.empty()?s->cmdQuit():gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); };
        commands["save"]=[s](const std::string& x){ return x.empty()?s->cmdSave():gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); };
        commands["load"]=[s](const std::string& x){ return x.empty()?s->cmdLoad():gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); };
        commands["walk"]=commands["move"]=commands["go"]=[s](const std::string& x){ return s->cmdMove(x); };
        commands["take"]=commands["pick up"]=commands["get"]=[s](const std::string& x){ return s->cmdTake(x); };
        commands["drop"]=commands["leave"]=[s](const std::string& x){ return s->cmdDrop(x); };
        commands["i"]=commands["inventory"]=[s](const std::string& x){ return x.empty()?s->cmdInv():gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); };
        commands["look"]=[s](const std::string& x){
            if(x.empty()) return s->cmdLook();
            auto res=s->resolveWithHolder(x);
            if(!res.obj) return gStr(res.error);
            return gStr("Where did you learn English?");
        };
        commands["look around"]=[s](const std::string& x){ return x.empty()?s->cmdLook():gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); };
        commands["ex"]=commands["exam"]=commands["examine"]=commands["look at"]=commands["inspect"]=[s](const std::string& x){ return s->cmdExam(x); };
        commands["put"]=[s](const std::string& x){ return s->cmdPut(x); };
        commands["talk"]=commands["talk to"]=[s](const std::string& x){ return s->cmdTalk(x); };
        commands["use"]=[s](const std::string& x){ return s->cmdUse(x); };
        commands["open"]=[s](const std::string& x){ return s->cmdOpen(x); };
        commands["close"]=[s](const std::string& x){ return s->cmdClose(x); };
        commands["ride"]=commands["mount"]=[s](const std::string& x){ return s->cmdRide(x); };
        commands["enter"]=[s](const std::string& x){ return s->cmdEnter(x); };
        commands["give"]=[s](const std::string& x){ return s->cmdGive(x); };
        commands["read"]=[s](const std::string& x){ return s->cmdRead(x); };
        commands["press"]=commands["push"]=[s](const std::string& x){ return s->cmdPress(x); };
        commands["dig"]=[s](const std::string& x){ return s->cmdDig(x); };
        commands["wear"]=commands["put on"]=[s](const std::string& x){ return s->cmdWear(x); };
        commands["remove"]=commands["take off"]=[s](const std::string& x){ return s->cmdUnwear(x); };
        commands["exit"]=commands["walk out"]=commands["go out"]=commands["move out"]=commands["out"]=[s](const std::string& x){ return x.empty()?s->cmdExit():gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); };
        for (const std::string& c : {std::string("walk"),std::string("go"),std::string("move"),std::string("enter")})
            commands[c+" exit"]=[s](const std::string& x){ return x.empty()?s->cmdExit():gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); };
        for (const std::string& d : {std::string("north"),std::string("south"),std::string("east"),std::string("west")})
            for (const std::string& dd : {d, std::string(1,d[0])})
                commands[dd]=[s,dd](const std::string& x){ return x.empty()?s->cmdMove(dd):gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); };
        ongoingcmds={"pick","look","talk","put","take","walk","go","move","enter"};
    }

    // ── reset (mirrors Game.reset) ─────────────────────────────
    // Builds a fresh island and places the player, with no screen output of
    // its own. Called from setup() (full intro) and from cmdLoad() (silent,
    // no intro — mirrors Python's self.reset(), which runs on the same
    // long-lived Game object both at startup and to restore state before a
    // load replay — never allocating a fresh Engine the way "play again" does).
    void reset() {
        World* w = new World({10,10});
        auto* table  = new Table(w->positions[{5,5}]);
        new Apple(table);
        auto* box = new LockedBox(table);
        new PasswordNote(box,PASSWORDS[0]);
        new StartingMan(w->positions[{4,5}]);
        new StartingPath(w,{6,5},{8,5});
        for(int x=0;x<10;++x) new StartingDitch(w->positions[{x,6}],2);
        for(int x=0;x<6;++x) for(int y=7;y<10;++y) w->inside({x,y},"village","in");
        new StartingHorse(w->positions[{7,5}], w->positions[{7,7}], "I jump over the \x1b[1m\x1b[38;5;136mditch\x1b[0m on the horse.");
        new StartingForest(w->positions[{7,4}], w->positions[{7,5}]);
        new MainDevice(w->positions[{7,7}]);
        auto* cwall = new ContainerWall(w->positions[{9,5}],4);
        new Coin(cwall);
        auto* archway = new Archway(w->positions[{6,7}]);
        new Watchman(archway, w->positions[{7,7}]);
        w->positions[{6,7}]->point("village","","","west");
        w->positions[{5,7}]->point("archway","leading out of the village","","east","archway");
        for(int y=8;y<10;++y) new NormalWall(w->positions[{6,y}],3);
        auto* house = new House(w->positions[{5,8}],w->positions[{5,7}]);
        new JokeMan(house->positions[{1,0}], {
            "Hi, my name is Transylvanian Cross-Country Discombobulating Green Apple Cooker Rajuson B. B. Jeff Herfet. (try talking to me again)",
            "Look! I'm inside a house! Hey, have you ever seen a house before? (try talking to me again)",
            "Woooo! I'm flying! Yay! (try talking to me again)",
            "My name is Jeff! (try talking to me again)",
            "A, b, c, d, e, f, g ... w, x, y, and z! Now I know my ABC, I can finally learn my numbers. Hey, do you know numbers? I've heard they're really hard to learn. (try talking to me again)",
            "Look! It's a polar bear! Ha, I tricked you! (try talking to me again)",
            "I love eating baby food, but dog biscuits are also not bad. (try talking to me again)",
            "Hello. This is my house, I live here. You're welcome back at any time! (try talking to me again)"
        }, w->positions[{4,7}]);
        new SandPatch(w->positions[{3,5}],
            [](ContainerObject* pos)->GameObject*{ return new CloakChest(pos); }, "I uncover a \x1b[1m\x1b[38;5;136mchest\x1b[0m!");
        new Ship(w->positions[{9,7}], w->positions[{8,7}]);
        Person* p = new Person(w->positions[{5,5}]);
        world = w;
        person = p;
        updateIdx();
        donecommands=0;
        gInputs.clear();
        gQueuedInputs.clear();
    }

    // ── setup (mirrors Game.setup) ─────────────────────────────
    // Resets the world and shows the title/intro/tips screens, finally
    // looking around. Called once from initGame() and again by main()'s
    // restart loop after "play again" (Python calls it again on the same
    // Game object; C++ instead allocates a fresh Engine each restart).
    void setup() {
        cls();
        reset();
        pyprint(
            "\x1b[1m\x1b[32m\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x97\n"
            "\xe2\x95\x91 QUEST FOR THE FIVE KEYS \xe2\x95\x91\n"
            "\xe2\x95\x91\x1b[39m\x1b[3m A text adventure game\x1b[32m\x1b[23m   \xe2\x95\x91\n"
            "\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d\x1b[0m\n"
            "\n");
        getInputWrapped("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m");

        cls();
        pyprint(
            "You are one of the world's foremost research scientists. After years of work, "
            "you had finally completed the greatest experiment of your career.\n\n"
            "Before you could present your discovery, your rivals stole the results of your experiment "
            "and fled to a remote island. There they took extraordinary measures to ensure no one could recover your work.\n\n"
            "The complete result is sealed inside a high-security electronic device of your own making "
            "that you were using to store your work. It can only be opened by entering \x1b[1m\x1b[32mfive different passwords\x1b[0m, "
            "each hidden somewhere on the island. Beware! Enter a single incorrect password and the device will "
            "destroy itself, taking your experiment with it forever.\n\n"
            "The island is inhabited. Its people know nothing of your rivals' actions, but some may help you "
            "if you can persuade them, while others may have something you need.\n\n"
            "Can you recover the five passwords, unlock the device, and reclaim your stolen work? "
            "Your success depends entirely on your ingenuity.\n\n"
            "Your fate-and the fate of your experiment-is now in your hands.\n");
        getInputWrapped("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m");

        cls();
        pyprint(
            "To help you in your quest, here are some tips:\n\n"
            "Always examine objects, however unrelated you think they are.\n\n"
            "Always follow paths to their end.\n\n"
            "Don't go anywhere you can't see anything around, you will just waste commands.\n\n"
            "Every NPC in the game does something, to help you, or to kill you.\n\n"
            "When talking to an NPC, you can only select one of the numbered options given by the game "
            "by typing the exact number you want. For example, you might be given a prompt like options:1/2/3. "
            "Then, you will be able to select 1, 2, or 3.\n\n"
            "If a command does not work, try using another word with the same meaning.\n\n"
            "Moving in any direction always also looks around. You don't need to retype look after going somewhere.\n\n"
            "You can always interact with an object if it is in view (listed in 'look').\n");
        getInputWrapped("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m");

        cls();
        pyprint(
            "When interacting with an object in any way (pick up, examine, talk to, etc), you don't need to type the "
            "full name. You can usually use only one word. For example, take box instead of take locked box, "
            "exam device instead of exam electronic device, etc.\n\n"
            "If an object inside a container is not listed in look around (if it is inside a container inside another container), "
            "you can access it with \x1b[1mcommand\x1b[0m \x1b[3mobject in container\x1b[0m. For example, take apple will not work when the apple "
            "is inside a box which is on a table, but take apple from box will.\n\n"
            "You can also use abbreviations for commands, like exam instead of examine, talk instead of talk to, etc. "
            "They are also given in the help.\n\n"
            "You are allowed to use a maximum of "+std::to_string(maxcommands)+" commands, including 'help'. After that, it will be too late "
            "to recover your work and you will have lost the game.\n\n"
            "Type 'moves' or 'commands' at any time to see the number of commands you have left.\n\n"
            "Type 'save' to export a file from which you can later continue play. Load a saved file with 'load'.\n\n"
            "Quitting is for losers, but you can do it by typing 'quit'.\n\n"
            "Good luck!\n");
        getInputWrapped("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m");

        cls();
        reset();
        auto il=cmdLook();
        if(il.isStr()) pyprint(il.asStr()+"\n");
    }

    // ── generic holding accessor ─────────────────────────────
    static std::set<GameObject*>* holdingOf(GameObject* o) {
        return o->holdingPtr();
    }
    static std::string typeOf(GameObject* o) { return propStr(o->properties,"type"); }

    bool isContainer(GameObject* o) {
        auto t=typeOf(o);
        return t=="container"||t=="blocking-container";
    }
    static bool isOpenOrGateless(GameObject* o) {
        return !propHas(o->properties,"open")||propBool(o->properties,"open");
    }

    // ── anifier ──────────────────────────────────────────────
    std::string anifier(const std::string& word,
                        const WorldPosition::PointMap* pm=nullptr) const {
        if(pm) {
            auto it=pm->find(word);
            if(it!=pm->end()) {
                const std::string& thing=it->second.first;
                const std::string& pluralword=it->second.second;
                std::string suffix=thing.empty()?"":(" "+thing);
                if(!pluralword.empty())
                    return pluralword+" \x1b[1m\x1b[38;5;136m"+word+"\x1b[0m"+suffix;
                char c=word.empty()?0:std::tolower((unsigned char)word[0]);
                bool vowel=c=='a'||c=='e'||c=='i'||c=='o'||c=='u';
                return (vowel?"an ":"a ")+std::string("\x1b[1m\x1b[38;5;136m")+word+"\x1b[0m"+suffix;
            }
        }
        if(objectindex.count(word)) {
            if(propHas(objectindex.at(word)->properties,"pluralreference"))
                return propStr(objectindex.at(word)->properties,"pluralreference")+" \x1b[1m\x1b[38;5;136m"+word+"\x1b[0m";
        }
        if(!word.empty()){
            char c=std::tolower((unsigned char)word[0]);
            if(c=='a'||c=='e'||c=='i'||c=='o'||c=='u') return "an \x1b[1m\x1b[38;5;136m"+word+"\x1b[0m";
        }
        return "a \x1b[1m\x1b[38;5;136m"+word+"\x1b[0m";
    }

    std::string fmtPlural(const std::vector<std::string>& v,
                          const WorldPosition::PointMap* pm=nullptr) const {
        std::vector<std::string> a; for(auto& s:v) a.push_back(anifier(s,pm));
        if(a.size()==1) return a[0];
        if(a.size()==2) return a[0]+" and "+a[1];
        std::string r;
        for(size_t i=0;i+1<a.size();++i) r+=(i?", ":"")+a[i];
        return r+", and "+a.back();
    }

    // ── updateIdx ────────────────────────────────────────────
    void updateIdx() {
        aroundobjects.clear(); aroundpaths.clear(); objectindex.clear(); inventoryobjects.clear();

        // Path map
        std::map<std::pair<int,int>,PathEntry> pathmap;
        for(auto* path:world->paths) {
            int x0=std::min(path->a.first,path->b.first),x1=std::max(path->a.first,path->b.first);
            int y0=std::min(path->a.second,path->b.second),y1=std::max(path->a.second,path->b.second);
            for(int x=x0;x<=x1;++x)
                for(int y=y0;y<=y1;++y)
                    pathmap[{x,y}]={path,path->a,path->b,{x,y},path->direction};
        }

        auto [px,py]=person->position->worldposition;
        for(int xi:{-1,1,0}) for(int yi:{-1,1,0}) {
            int nx=px+xi, ny=py+yi;
            if(nx<0||nx>=world->size.first||ny<0||ny>=world->size.second) continue;
            std::pair<int,int> np{nx,ny};
            if(pathmap.count(np)) {
                auto& pe=pathmap.at(np);
                std::string pn=propStr(pe.path->properties,"object");
                aroundpaths[pn]=pe;
                objectindex[pn]=pe.path;
                std::string sec=propStr(pe.path->properties,"secondname");
                if(!sec.empty()) objectindex[sec]=pe.path;
            }
            for(auto* obj:world->positions.at(np)->holding) {
                // skip person sentinel
                if(obj==reinterpret_cast<GameObject*>(person)) continue;
                std::string key=propStr(obj->properties,"object");
                if(key=="person") continue;
                std::string sec=propStr(obj->properties,"secondname");
                aroundobjects[key]={obj,{xi,yi}};
                objectindex[key]=obj;
                if(!sec.empty()) objectindex[sec]=obj;
                if(isContainer(obj)&&isOpenOrGateless(obj)) {
                    if(auto* h=holdingOf(obj)) {
                        for(auto* inner:*h) {
                            std::string ik=propStr(inner->properties,"object");
                            aroundobjects[ik]={inner,{xi,yi},true};
                            objectindex[ik]=inner;
                            std::string is=propStr(inner->properties,"secondname");
                            if(!is.empty()) objectindex[is]=inner;
                        }
                    }
                }
            }
        }
        // inventory
        for(auto* obj:person->inventory.holding) {
            std::string key=propStr(obj->properties,"object");
            std::string sec=propStr(obj->properties,"secondname");
            inventoryobjects[key]=obj;
            objectindex[key]=obj;
            if(!sec.empty()) objectindex[sec]=obj;
            if(isContainer(obj)&&isOpenOrGateless(obj)) {
                if(auto* h=holdingOf(obj)) {
                    for(auto* inner:*h) {
                        objectindex[propStr(inner->properties,"object")]=inner;
                        std::string is=propStr(inner->properties,"secondname");
                        if(!is.empty()) objectindex[is]=inner;
                    }
                }
            }
        }
    }

    // ── resolveWithHolder (mirrors Python's Engine.resolve) ──
    struct Resolved { GameObject* obj=nullptr; std::string error; GameObject* holder=nullptr; std::string holdername; };
    Resolved resolveWithHolder(const std::string& inp,
                               const std::string& s1="on",
                               const std::string& s2="in",
                               const std::string& s3="from") {
        std::string objn, hn;
        auto trySplit=[&](const std::string& sep)->bool{
            size_t p=inp.find(" "+sep+" ");
            if(p==std::string::npos) return false;
            objn=inp.substr(0,p); hn=inp.substr(p+sep.size()+2); return true;
        };
        if(!trySplit(s1)&&!trySplit(s2)&&!trySplit(s3)) objn=inp;
        if(objn.substr(0,4)=="the ") objn=objn.substr(4);
        if(!hn.empty()&&hn.substr(0,4)=="the ") hn=hn.substr(4);

        if(hn.empty()) {
            if(!objectindex.count(objn)) return {nullptr,"I don't see that here."};
            auto* o=objectindex.at(objn);
            return {o,"",nullptr,"",};
        }
        if(!objectindex.count(hn)) return {nullptr,"I don't see that here."};
        auto* holder=objectindex.at(hn);
        std::string hname=propStr(holder->properties,"object"), htype=typeOf(holder);
        if(propHas(holder->properties,"open")&&!propBool(holder->properties,"open")) return {nullptr,"It's locked!"};
        if(htype!="container"&&htype!="blocking-container")
            return {nullptr,"\x1b[31mSorry, I don't understand.\x1b[0m"};
        if(auto* h=holdingOf(holder))
            for(auto* o:*h) {
                std::string sn=propStr(o->properties,"secondname");
                if(propStr(o->properties,"object")==objn || (!sn.empty()&&sn==objn))
                    return {o,"",holder,hname};
            }
        return {nullptr,"I don't see that "+propStr(holder->properties,"objectsare")+" the \x1b[1m\x1b[38;5;136m"+hname+"\x1b[0m.",nullptr,""};
    }

    // ── cmdInv ───────────────────────────────────────────────
    GameResult cmdInv() {
        if(!inventoryobjects.empty()) {
            std::vector<std::string> v; for(auto& kv:inventoryobjects) v.push_back(kv.first);
            std::sort(v.begin(),v.end());
            return gStr("I am carrying "+fmtPlural(v)+".");
        }
        return gStr("I am not carrying anything.");
    }

    // ── allworlds (mirrors Python's Engine.allworlds) ────────
    // Walks curworld = curworld.position.world up through nested InsideWorlds
    // (e.g. bedroom -> house -> outer world), collecting each one's name(s).
    std::unordered_set<std::string> allworlds() {
        std::unordered_set<std::string> r;
        WorldBase* curworld=world;
        while(true) {
            if(propStr(curworld->wprops,"type")!="inside-world") return r;
            auto* iw=curworld->asInsideWorld();
            r.insert(propStr(iw->properties,"object"));
            std::string sn=propStr(iw->properties,"secondname");
            if(!sn.empty()) r.insert(sn);
            auto* wp=iw->position->asWorldPosition();
            if(!wp) return r;
            curworld=wp->world;
        }
    }

    // ── cmdExit ──────────────────────────────────────────────
    GameResult cmdExit() {
        if(propStr(world->wprops,"type")=="world") return gStr("I'm already outside.");
        if(auto* iw=world->asInsideWorld()) {
            if(!iw->exitablepositions.empty() && !iw->exitablepositions.count(person->position->worldposition))
                return gStr("I can't go out from here.");
        }
        auto* outside=world->exitposition;
        world=outside->world;
        person->move(outside);
        updateIdx();
        return gStr("Going \x1b[1m\x1b[38;5;30mout\x1b[0m...\n\n"+cmdLook().asStr());
    }

    // ── cmdLook ──────────────────────────────────────────────
    GameResult cmdLook() {
        std::string r;
        bool objectsfound=false;
        if(propHas(person->position->properties,"inside")) {
            std::string pr=propStr(person->position->properties,"pluralreference");
            bool hasPr=propHas(person->position->properties,"pluralreference");
            std::string insideThing=propStr(person->position->properties,"inside");
            std::string insideDesc=hasPr
                ? pr+" \x1b[1m\x1b[38;5;136m"+insideThing+"\x1b[0m"
                : anifier(insideThing);
            r+="I am "+propStr(person->position->properties,"insidereference")+" "+insideDesc+".\n\n";
        }

        static const std::map<std::pair<int,int>,std::string> dirnames={
            {{0,0},"Right next to me"},{{0,1},"On my north"},{{0,-1},"On my south"},
            {{1,0},"On my east"},{{-1,0},"On my west"},{{1,1},"On my north-east"},
            {{-1,1},"On my north-west"},{{1,-1},"On my south-east"},{{-1,-1},"On my south-west"}
        };

        if(!aroundpaths.empty()) {
            auto mypos=person->position->worldposition;
            for(auto& [pname,pe]:aroundpaths) {
                std::string ps;
                objectsfound=true;
                if(pe.pos==mypos) ps="I am "+propStr(pe.path->properties,"insidereference")+" "+anifier(pname)+". It continues to my ";
                else {
                    int dx=pe.pos.first==mypos.first?0:(pe.pos.first>mypos.first?1:-1);
                    int dy=pe.pos.second==mypos.second?0:(pe.pos.second>mypos.second?1:-1);
                    auto it=dirnames.find({dx,dy});
                    std::string fullkey=it!=dirnames.end()?it->second:"";
                    std::string coloredDir;
                    if(fullkey.substr(0,5)=="On my")
                        coloredDir="On my \x1b[1m\x1b[38;5;30m"+fullkey.substr(6)+"\x1b[0m";
                    else
                        coloredDir="Right \x1b[1m\x1b[38;5;30mnext to me\x1b[0m";
                    std::transform(coloredDir.begin(),coloredDir.end(),coloredDir.begin(),::tolower);
                    ps="There is "+anifier(pname)+" "+coloredDir+". It leads ";
                }
                std::vector<std::string> de;
                if(pe.pos!=pe.a) de.push_back("\x1b[1m\x1b[38;5;30m"+pe.dir.first+"\x1b[0m");
                if(pe.pos!=pe.b) de.push_back("\x1b[1m\x1b[38;5;30m"+pe.dir.second+"\x1b[0m");
                for(size_t i=0;i<de.size();++i){if(i) ps+=" and "; ps+=de[i];}
                ps+="."; r+=ps+"\n";
            }
            r+="\n";
        }

        std::map<std::string,std::vector<std::string>> dirobjects;
        for(auto& [k,v]:dirnames) dirobjects[v];

        std::map<std::string,std::set<GameObject*>*> contHolding;
        for(auto& [name,ae]:aroundobjects) {
            if(person->position->skipSet.count(name)) continue;
            if(ae.inCont) continue;
            auto it=dirnames.find(ae.dir);
            std::string dn=it!=dirnames.end()?it->second:"";
            dirobjects[dn].push_back(name);
            if(isContainer(ae.obj)&&!propBool(ae.obj->properties,"hiddeninside"))
                if(auto* h=holdingOf(ae.obj)) contHolding[name]=h;
        }
        // Objects sharing a direction are listed alphabetically (mirrors Python's sorted dirobjects).
        for(auto& [dn,objs]:dirobjects) std::sort(objs.begin(),objs.end());

        static const std::vector<std::string> dirorder = {
            "Right next to me","On my north","On my south","On my east","On my west",
            "On my north-east","On my north-west","On my south-east","On my south-west"
        };
        auto& pospoints=person->position->points;
        for(const auto& dirkey : dirorder) {
            auto doIt=dirobjects.find(dirkey);
            std::vector<std::string> objs = doIt!=dirobjects.end()?doIt->second:std::vector<std::string>{};
            auto ppIt=pospoints.find(dirkey);
            WorldPosition::PointMap emptyPM;
            const auto& pp=(ppIt!=pospoints.end())?ppIt->second:emptyPM;
            if(pp.empty()&&objs.empty()) continue;
            objectsfound=true;
            if(dirkey.substr(0,5)=="On my")
                r+="On my \x1b[1m\x1b[38;5;30m"+dirkey.substr(6)+"\x1b[0m";
            else
                r+="Right \x1b[1m\x1b[38;5;30mnext to me\x1b[0m";
            bool plural;
            if(pp.empty()) {
                plural=objs.size()>1||(objs.size()==1&&
                    objectindex.count(objs[0])&&propBool(objectindex.at(objs[0])->properties,"plural"));
            } else {
                if(!objs.empty()) plural=true;
                else if(pp.size()>1) plural=true;
                else plural=!pp.begin()->second.second.empty();
            }
            r+=plural?" are ":" is ";
            std::vector<std::string> all=objs;
            for(auto& [pn,_]:pp) all.push_back(pn);
            std::sort(all.begin(),all.end());
            r+=fmtPlural(all,&pp)+".\n";

            for(auto& obj:objs) {
                if(contHolding.count(obj)&&!contHolding[obj]->empty()) {
                    auto* co=objectindex.count(obj)?objectindex.at(obj):nullptr;
                    std::string oa;
                    if(co) oa=propStr(co->properties,"objectsare");
                    if(!oa.empty()) oa[0]=std::toupper((unsigned char)oa[0]);
                    r+=oa+" the \x1b[1m\x1b[38;5;136m"+obj+"\x1b[0m ";
                    auto& h=*contHolding[obj];
                    bool hp=h.size()>1||(h.size()==1&&propBool((*h.begin())->properties,"plural"));
                    r+=(hp?"are ":"is ");
                    std::vector<std::string> hv; for(auto* x:h) hv.push_back(propStr(x->properties,"object"));
                    std::sort(hv.begin(),hv.end());
                    r+=fmtPlural(hv)+".\n";
                }
            }
            r+="\n";
        }
        if(!objectsfound) r="I don't see anything around here.";
        while(!r.empty()&&(r.back()=='\n'||r.back()==' ')) r.pop_back();
        cls();
        return gStr(r);
    }

    // ── cmdDig ───────────────────────────────────────────────
    GameResult cmdDig(const std::string& inp) {
        if(inp.empty()) return gStr("What should I dig?");
        auto res_=resolveWithHolder(inp);
        if(!res_.obj) return gStr(res_.error);
        auto* obj=res_.obj;
        if(!propBool(obj->properties,"digable")) return gStr("That's ridiculous.");
        if(propHas(obj->properties,"digtool")) {
            std::string toolname=propStr(obj->properties,"digtool");
            std::map<std::string,GameObject*> inv;
            for(auto* o:person->inventory.holding) inv[propStr(o->properties,"object")]=o;
            if(!inv.count(toolname)) {
                if(propHas(obj->properties,"nodigtoolmessage")) return gStr(propStr(obj->properties,"nodigtoolmessage"));
                return gStr("I need "+anifier(toolname)+" to do that...");
            }
            auto res=inv[toolname]->use(person,obj); updateIdx(); return res;
        }
        auto res=obj->dig(person); updateIdx(); return res;
    }

    // ── cmdRead ──────────────────────────────────────────────
    GameResult cmdRead(const std::string& inp) {
        if(inp.empty()) return gStr("What should I read?");
        auto res=resolveWithHolder(inp);
        if(!res.obj) return gStr(res.error);
        if(typeOf(res.obj)!="note") return gStr("That's ridiculous.");
        return gStr(res.obj->read());
    }

    // ── cmdPress ─────────────────────────────────────────────
    GameResult cmdPress(const std::string& inp) {
        if(inp.empty()) return gStr("What should I press?");
        auto res=resolveWithHolder(inp);
        if(!res.obj) return gStr(res.error);
        if(typeOf(res.obj)!="button") return gStr("That's ridiculous.");
        return res.obj->press(person);
    }

    // ── cmdOpen / cmdClose ───────────────────────────────────
    GameResult cmdOpen(const std::string& inp) {
        if(inp.empty()) return gStr("What should I open?");
        auto res_=resolveWithHolder(inp);
        if(!res_.obj) return gStr(res_.error);
        auto* obj=res_.obj;
        if(!propHas(obj->properties,"open")) return gStr("That's ridiculous.");
        if(propBool(obj->properties,"open")) return gStr(propBool(obj->properties,"plural")?"They're already open.":"It's already open.");
        if(propHas(obj->properties,"key")) {
            std::string keyname=propStr(obj->properties,"key");
            std::map<std::string,GameObject*> inv;
            for(auto* o:person->inventory.holding) inv[propStr(o->properties,"object")]=o;
            if(!inv.count(keyname))
                return gStr("I need "+anifier(keyname)+" to open "+(propBool(obj->properties,"plural")?"them":"it")+"...");
            auto res=inv[keyname]->use(person,obj); updateIdx(); return res;
        }
        auto res=obj->openContainer(person); updateIdx(); return res;
    }
    GameResult cmdClose(const std::string& inp) {
        if(inp.empty()) return gStr("What should I close?");
        auto res_=resolveWithHolder(inp);
        if(!res_.obj) return gStr(res_.error);
        auto* obj=res_.obj;
        if(!propHas(obj->properties,"open")) return gStr("That's ridiculous.");
        if(!propBool(obj->properties,"open")) return gStr(propBool(obj->properties,"plural")?"They're already closed.":"It's already closed.");
        if(propHas(obj->properties,"key")) {
            std::string keyname=propStr(obj->properties,"key");
            std::map<std::string,GameObject*> inv;
            for(auto* o:person->inventory.holding) inv[propStr(o->properties,"object")]=o;
            if(!inv.count(keyname))
                return gStr("I need "+anifier(keyname)+" to close "+(propBool(obj->properties,"plural")?"them":"it")+"...");
            auto res=inv[keyname]->use(person,obj); updateIdx(); return res;
        }
        auto res=obj->closeContainer(person); updateIdx(); return res;
    }

    // ── prompt (mirrors Game.prompt: yes/no, 3 tries, then default) ──
    static std::string stripStr(const std::string& s) {
        size_t a=0,b=s.size();
        while(a<b&&isspace((unsigned char)s[a])) ++a;
        while(b>a&&isspace((unsigned char)s[b-1])) --b;
        return s.substr(a,b-a);
    }
    bool promptYN(const std::string& promptText, bool defaultVal=false) {
        std::string ans=getInputWrapped(promptText);
        std::string low=stripStr(ans);
        std::transform(low.begin(),low.end(),low.begin(),::tolower);
        if(low.empty()) return defaultVal;
        char c=low[0];
        if(c=='y') return true;
        if(c=='n') return false;
        for(int i=0;i<2;++i) {
            std::string p2="\x1b[1m\x1b[31m[invalid input ("+std::to_string(i+2)+"/3)\x1b[0m "+promptText;
            std::string ans2=getInputWrapped(p2);
            // Mirrors Python exactly: retries compare the raw (unstripped, uncased) answer.
            if(ans2=="y") return true;
            if(ans2=="n") return false;
        }
        return defaultVal;
    }

    // ── getFile (mirrors Game.getfile) ────────────────────────
    static std::string expandUser(const std::string& p) {
        if(p.empty()||p[0]!='~') return p;
        if(p.size()==1||p[1]=='/') {
            const char* home=std::getenv("HOME");
            return (home?std::string(home):std::string())+p.substr(1);
        }
        return p; // "~username" form is left unexpanded (rare edge case)
    }
    std::optional<std::string> getFile(const std::string& filefor) {
        std::string promptText=std::string("\x1b[1m")+(filefor=="save"?"Save":"Load")+" File: \x1b[0m";
        std::string raw=getInputWrapped(promptText);
        std::string fn=stripStr(raw);
        if(fn.empty()) return std::nullopt;
        fn=expandUser(fn);
        std::error_code ec;
        std::filesystem::path abs=std::filesystem::absolute(fn, ec);
        fn=abs.string();
        bool exists=std::filesystem::exists(fn, ec);
        bool isDir=std::filesystem::is_directory(fn, ec);
        if(filefor=="save"&&exists) {
            if(isDir) { pyprint("\x1b[31merror: already existing directory\x1b[0m\n"); return std::nullopt; }
            if(!promptYN("\x1b[1m\x1b[33mfile already exists. overwrite? (y/N): \x1b[0m")) return getFile(filefor);
            return fn;
        } else if(filefor=="load"&&!exists) {
            pyprint("\x1b[31merror: file does not exist\x1b[0m\n");
            return std::nullopt;
        } else if(filefor=="load"&&isDir) {
            pyprint("\x1b[31merror: is a directory\x1b[0m\n");
            return std::nullopt;
        }
        return fn;
    }

    // ── cmdQuit (mirrors Game.quit) ────────────────────────────
    GameResult cmdQuit() {
        if(hasRealProgress()&&promptYN("\x1b[1m\x1b[33msave game before quitting? (y/N): \x1b[0m")) {
            // Mirrors Python's `if saveoutput := self.save(): printoutput(saveoutput)`.
            auto saveResult=cmdSave();
            if(saveResult.isStr()&&!saveResult.asStr().empty()) pyprint(saveResult.asStr()+"\n");
        }
        return gEnd("","bye",false);
    }

    // ── cmdSave (mirrors Game.save) ───────────────────────────
    // Returns a default (None-kind) GameResult for the "getfile canceled"
    // case, mirroring Python's bare `return` there (real None) rather than a
    // falsy string — cmdQuit() needs to tell them apart to reproduce
    // printoutput(self.save()) printing the literal text "None" in that case.
    GameResult cmdSave() {
        auto fn=getFile("save");
        if(!fn) return GameResult{};
        std::ofstream file(*fn, std::ios::binary);
        if(!file) return gStr("\x1b[31merror: could not open file\x1b[0m");
        std::string joined;
        for(size_t i=0;i<gInputs.size();++i){ if(i) joined+="\n"; joined+=gInputs[i]; }
        file.write(joined.data(), (std::streamsize)joined.size());
        if(!file) return gStr("\x1b[31merror: write failed\x1b[0m");
        return gStr("\x1b[32m\x1b[3msaved game\x1b[0m");
    }

    // ── cmdLoad (mirrors Game.load) ───────────────────────────
    // Replays a saved command transcript through parse(), duplicating loop()'s
    // own EndGame/move-counting handling exactly (Python does not factor this
    // into a shared helper either — it is inlined twice, in loop() and here).
    GameResult cmdLoad() {
        if(hasRealProgress()&&!promptYN("\x1b[1m\x1b[33mdiscard current game? (y/N): \x1b[0m"))
            return gStr("");
        auto fnOpt=getFile("load");
        if(!fnOpt) return gStr("");
        std::ifstream file(*fnOpt, std::ios::binary);
        // Python's load() wraps this whole section in try/except, and now (unlike
        // save()) exits the whole program on failure via exitwith(); getfile()
        // already validated existence for "load", so this can only fail here on
        // a rare race/permission error — mirror that hard-exit behavior exactly.
        if(!file) exitWithError("\x1b[31merror: could not open file\x1b[0m");
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        reset();
        gQueuedInputs.clear();
        { size_t start=0; for(size_t i=0;i<=content.size();++i) if(i==content.size()||content[i]=='\n') { gQueuedInputs.push_back(content.substr(start,i-start)); start=i+1; } }
        while(!gQueuedInputs.empty()) {
            std::string command=getInputWrapped("\n> ");
            pyprint("\n");
            auto output=parse(command);
            if(output.isEnd()) {
                if(output.eg.description.empty()&&!output.eg.win) { pyprint(output.eg.endmessage+"\n"); return gStr(""); }
                pyprint("\x1b[H\x1b[2J\x1b[3J");
                pyprint("\x1b[3m"+output.eg.description+"\x1b[0m\n\n"+output.eg.endmessage+"\n\n"+
                    (output.eg.win?"\x1b[1m\x1b[32mYOU WIN!\x1b[0m":"\x1b[1m\x1b[31mYOU LOSE.\x1b[0m")+"\n");
                bool playagain=false;
                while(true) {
                    std::string pa=getInputWrapped("\x1b[1m\x1b[32mDo you want to play again? (yes/no): \x1b[0m");
                    std::string palow=stripStr(pa); std::transform(palow.begin(),palow.end(),palow.begin(),::tolower);
                    if(palow=="yes"){ playagain=true; break; }
                    if(palow=="no"){ playagain=false; break; }
                    pyprint("That is not a valid option.\n\n");
                }
                if(playagain) break; // mirrors Python: breaks the replay loop only, does not re-run setup()
                pyprint("\nbye\n");
                return gStr("");
            }
            if(output.isStr()&&!output.asStr().empty()) {
                pyprint(output.asStr()+"\n");
                int remain=maxcommands-donecommands-1;
                if(remain>0&&remain<=20) pyprint("\n\n\x1b[31mYou only have "+std::to_string(remain)+" "+(remain!=1?"commands":"command")+" left!\x1b[0m\n");
                else if(remain==0) pyprint("\n\n\x1b[31mYou have 0 commands left!\x1b[0m\n");
                std::string clow=stripStr(command); std::transform(clow.begin(),clow.end(),clow.begin(),::tolower);
                if(clow!="moves"&&clow!="commands"&&output.asStr()!="\x1b[31mSorry, I don't understand.\x1b[0m"&&output.asStr()!="Time passes...") {
                    if(++donecommands==maxcommands) {
                        pyprint("\n\x1b[31mOh no! It is too late. Your rivals have come back to the island and destroyed the device! You have now lost your hard work forever.\x1b[0m\n\n");
                        bool playagain=false;
                        while(true) {
                            std::string pa=getInputWrapped("\x1b[1m\x1b[32mDo you want to play again? (yes/no): \x1b[0m");
                            std::string palow=stripStr(pa); std::transform(palow.begin(),palow.end(),palow.begin(),::tolower);
                            if(palow=="yes"){ playagain=true; break; }
                            if(palow=="no"){ playagain=false; break; }
                            pyprint("That is not a valid option.\n\n");
                        }
                        if(playagain) break;
                        pyprint("\nbye\n");
                        return gStr("");
                    }
                }
            }
        }
        return gStr("");
    }

    // ── cmdHelp ──────────────────────────────────────────────
    GameResult cmdHelp() {
        emit("\x1b[?1049h");
        pyprintKeepNL(
            "When at the > prompt, type actions in the format:\n\n"
            "    \x1b[1mcommand\x1b[0m \x1b[3minput\x1b[0m\n\n"
            "Examples: '\x1b[1mtake\x1b[0m \x1b[3mapple from table\x1b[0m', '\x1b[1mgive\x1b[0m \x1b[3mapple to man\x1b[0m', '\x1b[1mexam\x1b[0m \x1b[3mditch\x1b[0m'\n"
            "If the game returns 'Sorry, I don't understand.' for your command, try using another word with the same meaning.\n"
            "You have "+std::to_string(maxcommands)+" total commands to finish the game before you lose. Empty inputs and commands which return 'Sorry, I don't understand.' will not be counted.\n"
            "When talking to an NPC, you will be given numbered options like this after the NPC dialogue:\n\n"
            "    Hello.\n\n"
            "    \x1b[3m[1] Hi\x1b[0m\n"
            "    \x1b[3m[2] Bye\x1b[0m\n\n"
            "    \x1b[3moptions: 1/2\x1b[0m.\n\n"
            "Then you will have to enter only a number from the given options. You cannot type normal game commands here or exit this prompt yourself.\n");
        emit("\n");
        getInputWrapped("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m");
        emit("\x1b[H\x1b[2J");
        pyprintKeepNL(
            "Always try to examine all objects you see.\n"
            "All NPCs in the game are for a purpose, whether to help you, or to kill you.\n"
            "All passwords are of a similar type. (Example: 123, 456, ... or abc, def, ...)\n"
            "No passwords look very different from the others.\n"
            "Don't go any place where you can't see anything around.\n"
            "If an object inside a container is not listed in look around (if it is inside a container inside another container), you can access it with \x1b[1mcommand\x1b[0m \x1b[3mobject in container\x1b[0m. For example, \x1b[1mtake\x1b[0m \x1b[3mapple\x1b[0m will not work when the apple is inside a box which is on a table, but \x1b[1mtake\x1b[0m \x1b[3mapple from box\x1b[0m will.\n"
            "Moving in any direction always also looks around, you don't need to retype look.\n\n"
            "\x1b[1m\x1b[4mUseful commands:\x1b[0m\n\n"
            "- \x1b[1mnorth\x1b[0m / \x1b[1msouth\x1b[0m / \x1b[1meast\x1b[0m / \x1b[1mwest\x1b[0m / \x1b[1mout\x1b[0m\n"
            "(or \x1b[1mwalk\x1b[0m / \x1b[1mgo\x1b[0m / \x1b[1mmove\x1b[0m \x1b[3mnorth\x1b[0m / \x1b[3msouth\x1b[0m / \x1b[3meast\x1b[0m / \x1b[3mwest\x1b[0m / \x1b[3mout\x1b[0m)\n"
            "- \x1b[1mtake\x1b[0m / \x1b[1mpick up\x1b[0m / \x1b[1mget\x1b[0m \x1b[3mobject\x1b[0m\n"
            "- \x1b[1mex\x1b[0m / \x1b[1mexam\x1b[0m / \x1b[1mexamine\x1b[0m / \x1b[1mlook at\x1b[0m / \x1b[1minspect\x1b[0m \x1b[3mobject\x1b[0m\n"
            "- \x1b[1mtalk\x1b[0m / \x1b[1mtalk to\x1b[0m \x1b[3mperson\x1b[0m\n"
            "- \x1b[1muse\x1b[0m \x1b[3mobject\x1b[0m\n"
            "- \x1b[1muse\x1b[0m \x1b[3mobject_1 on object_2\x1b[0m\n");
        emit("\n");
        getInputWrapped("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m");
        emit("\x1b[?1049l");
        return gStr("\x1b[3mHelp done.\x1b[0m");
    }

    // ── cmdExam ──────────────────────────────────────────────
    GameResult cmdExam(const std::string& inp) {
        if(inp.empty()) return gStr("What should I examine?");
        auto res_=resolveWithHolder(inp);
        if(!res_.obj) return gStr(res_.error);
        auto* obj=res_.obj;
        auto& p=obj->properties;
        std::string other=propStr(p,"other"),color=propStr(p,"color"),material=propStr(p,"material");
        double heightV=propNumD(p,"height",0);
        bool hasHeight=propHas(p,"height")&&heightV!=0;
        std::string heightS=fmtNum(heightV);
        int width=propInt(p,"width",0);
        std::string message=propStr(p,"message"),objname=propStr(p,"object");
        bool plural=propBool(p,"plural");
        std::string ref=propHas(p,"reference")?propStr(p,"reference"):"it";
        ref[0]=std::toupper((unsigned char)ref[0]);
        std::string fw;
        if(hasHeight) fw=heightS;
        else if(width) fw=std::to_string(width);
        else if(!other.empty()) fw=other;
        else if(!color.empty()) fw=color;
        else fw=objname;
        std::string pr=propStr(p,"pluralreference");
        bool hasPr=propHas(p,"pluralreference");
        std::string r=ref+(plural?" are ":" is ");
        char fc=fw.empty()?'x':std::tolower((unsigned char)fw[0]);
        bool v=fc=='a'||fc=='e'||fc=='i'||fc=='o'||fc=='u';
        r+=hasPr?pr+" ":(v?"an ":"a ");
        if(hasHeight){ r+=heightS+" "+(heightV!=1?"meters":"meter")+" high";
            if(width) r+=" and "+std::to_string(width)+" "+(width!=1?"meters":"meter")+" wide"; }
        else if(width) r+=std::to_string(width)+" "+(width!=1?"meters":"meter")+" wide";
        if((hasHeight||width)&&(!other.empty()||!color.empty())) r+=" ";
        if(!other.empty()) r+=other+" ";
        if(!color.empty()) r+=color+" ";
        r+=objname;
        std::string otherafter=propStr(p,"otherafter");
        if(!otherafter.empty()) r+=", "+otherafter;
        if(!material.empty()) {
            bool materialnoof=propBool(p,"materialnoof");
            r+=", made"+std::string(materialnoof?"":" of")+" "+material+".";
        } else r+=".";
        std::string type=typeOf(obj);
        if((type=="container"||type=="blocking-container") && (!propHas(p,"open")||propBool(p,"open"))) {
            if(auto* h=holdingOf(obj)) if(!h->empty()) {
                std::string oa=propStr(p,"objectsare"); if(!oa.empty()) oa[0]=std::toupper((unsigned char)oa[0]);
                std::string sn=propHas(p,"secondname")?propStr(p,"secondname"):objname;
                r+=" "+oa+" the "+sn+" ";
                bool hp=h->size()>1||(h->size()==1&&propBool((*h->begin())->properties,"plural"));
                r+=(hp?"are ":"is ");
                std::vector<std::string> hv; for(auto* x:*h) hv.push_back(propStr(x->properties,"object"));
                std::sort(hv.begin(),hv.end());
                r+=fmtPlural(hv)+".";
            }
        }
        if(!message.empty()) r+=" "+message;
        return gStr(r);
    }

    // ── dialogue runner ──────────────────────────────────────
    GameResult runDialogue(const std::string& pname, DPtr cur) {
        bool first=true;
        while(cur) {
            if(!first) pyprint("\n");
            first=false;
            if(cur->endgame) return GameResult::fromEnd(cur->endgame->description,cur->endgame->endmessage,cur->endgame->win);
            if(!cur->giveItems.empty()) {
                std::vector<std::string> gv; for(auto* gi:cur->giveItems) gv.push_back(propStr(gi->properties,"object"));
                std::sort(gv.begin(),gv.end());
                std::string ref="It";
                if(objectindex.count(pname)){auto* o=objectindex.at(pname);auto rr=propStr(o->properties,"reference");if(!rr.empty()){ref=rr;ref[0]=std::toupper((unsigned char)ref[0]);}}
                updateIdx();
                return gStr("\x1b[3m"+ref+" gives me "+fmtPlural(gv)+".\x1b[0m\n"+cur->text);
            }
            if(cur->options.empty()) { updateIdx(); return gStr(cur->text); }
            pyprint(cur->text+"\n\n");
            std::vector<std::string> opts; int i=1;
            for(auto& [lbl,_]:cur->options){ opts.push_back(lbl); pyprint("\x1b[3m["+std::to_string(i++)+"] "+lbl+"\x1b[0m\n"); }
            while(true) {
                std::string line="\n\x1b[3moptions: ";
                for(int j=1;j<=(int)opts.size();++j){ line+=std::to_string(j); if(j<(int)opts.size()) line+="/"; }
                line+=".\x1b[0m";
                std::string ch=getInputWrapped(line+" ");
                // Mirror Python: chosen.strip(); int(chosen) must parse the WHOLE string;
                // int(chosen) <= 0 is invalid; otherwise chosableoptions[int-1] (1..len).
                size_t a=0,b=ch.size();
                while(a<b&&isspace((unsigned char)ch[a])) ++a;
                while(b>a&&isspace((unsigned char)ch[b-1])) --b;
                std::string t=ch.substr(a,b-a);
                bool isInt=!t.empty(); size_t sgn=(isInt&&(t[0]=='+'||t[0]=='-'))?1:0;
                if(sgn>=t.size()) isInt=false;
                for(size_t k=sgn;k<t.size()&&isInt;++k) if(!isdigit((unsigned char)t[k])) isInt=false;
                bool ok=false;
                if(isInt&&t.size()-sgn<=9) {           // digit-count cap avoids atol overflow UB
                    long v=std::atol(t.c_str());
                    if(v>=1&&v<=(long)opts.size()){ cur=cur->options[opts[v-1]]; ok=true; }
                }
                if(ok) break;
                pyprint("That is not a valid option.\n");
            }
        }
        return gStr("");
    }

    // ── cmdGive ──────────────────────────────────────────────
    GameResult cmdGive(const std::string& inp) {
        if(inp.empty()) return gStr("What should I give to who?");
        size_t p=inp.find(" to ");
        if(p==std::string::npos) {
            std::string n=inp; if(n.substr(0,4)=="the ") n=n.substr(4);
            if(objectindex.count(n)&&!person->inventory.holding.count(objectindex.at(n))) return gStr("I don't have that.");
            if(objectindex.count(n)) return gStr("Who should I give it to?");
            return gStr("I don't have that.");
        }
        std::string on=inp.substr(0,p), pn=inp.substr(p+4);
        if(on.substr(0,4)=="the ") on=on.substr(4);
        if(pn.substr(0,4)=="the ") pn=pn.substr(4);
        if(!objectindex.count(pn)) return gStr("I don't see that here.");
        if(!objectindex.count(on)) return gStr("I don't have that.");
        auto* obj=objectindex.at(on); auto* target=objectindex.at(pn);
        on=propStr(obj->properties,"object"); pn=propStr(target->properties,"object");
        if(!person->inventory.holding.count(obj)) return gStr("I don't have that.");
        if(person->wearing.count(obj)) return gStr("Remove it first.");
        if(typeOf(target)!="npc") return gStr("I don't think the "+pn+" wants it.");
        auto* npc=target->asNPC();
        auto [accepted,dil]=npc->give(obj,person);
        if(!accepted) {
            if(dil) return runDialogue(pn,dil);
            bool pl=propBool(obj->properties,"plural");
            std::string ref=propStr(target->properties,"reference"); if(ref.empty()) ref="it"; ref[0]=std::toupper((unsigned char)ref[0]);
            return gStr(ref+" refuses "+(pl?"them":"it")+".");
        }
        obj->deleteObj();
        pyprint("Gave \x1b[1m\x1b[38;5;136m"+on+"\x1b[0m to \x1b[1m\x1b[38;5;136m"+pn+"\x1b[0m.\n");
        if(dil) { pyprint("\n"); updateIdx(); return runDialogue(pn,dil); }
        return gStr("");
    }

    // ── cmdTalk ──────────────────────────────────────────────
    GameResult cmdTalk(const std::string& inp, DPtr given=nullptr) {
        if(!given && inp.empty()) return gStr("Who should I talk to?");
        std::string pn=inp; if(pn.substr(0,4)=="the ") pn=pn.substr(4);
        if(!objectindex.count(pn)) return gStr("I don't see that here.");
        auto* target=objectindex.at(pn); pn=propStr(target->properties,"object");
        if(typeOf(target)!="npc") return gStr("Hello, "+pn+"!");
        auto* npc=target->asNPC();
        auto dil=given?given:npc->dialogues(person);
        return runDialogue(pn,dil);
    }

    // ── cmdMove ──────────────────────────────────────────────
    // ── cmdEnter ─────────────────────────────────────────────
    GameResult cmdEnter(const std::string& inp_) {
        if(inp_.empty()) return gStr("What should I enter?");
        std::string gd=inp_;
        auto strip=[&](const std::string& pre){ if(gd.size()>=pre.size()&&gd.substr(0,pre.size())==pre) gd=gd.substr(pre.size()); };
        strip("in "); strip("inside "); strip("into "); strip("to ");
        if(gd.substr(0,4)=="the ") gd=gd.substr(4);
        if(allworlds().count(gd)) return gStr("I am already in the "+gd+"!");
        if(!objectindex.count(gd)) return gStr("I don't see that here.");
        auto* place=objectindex.at(gd);
        if(typeOf(place)!="inside-world") return gStr("That's ridiculous.");
        return cmdMove(gd);
    }

    GameResult cmdMove(const std::string& inp_) {
        if(inp_.empty()) return gStr("Where should I go?");
        std::string gd=inp_;
        auto strip=[&](const std::string& pre){ if(gd.size()>=pre.size()&&gd.substr(0,pre.size())==pre) gd=gd.substr(pre.size()); };
        strip("in "); strip("inside "); strip("into "); strip("to ");
        if(gd.substr(0,4)=="the ") gd=gd.substr(4);

        auto [cx,cy]=person->position->worldposition;
        if(allworlds().count(gd)) return gStr("I am already in the "+gd+"!");
        std::string dir_;
        std::pair<int,int> cardPos={-999,-999};
        bool isCard=false;

        if(gd=="n"||gd=="north"){ dir_="north"; cardPos={cx,cy+1}; isCard=true; }
        else if(gd=="s"||gd=="south"){ dir_="south"; cardPos={cx,cy-1}; isCard=true; }
        else if(gd=="w"||gd=="west"){ dir_="west"; cardPos={cx-1,cy}; isCard=true; }
        else if(gd=="e"||gd=="east"){ dir_="east"; cardPos={cx+1,cy}; isCard=true; }
        else if(objectindex.count(gd)) {
            auto* obj=objectindex.at(gd);
            std::string type=typeOf(obj);
            if(type=="inside-world") {
                auto* iw=obj->asInsideWorld();
                auto* targetWP=iw->startingpos;
                dir_=propStr(obj->properties,"insidereference")+" "+propStr(obj->properties,"object");
                // blocking/guarding
                auto* owp=obj->position->asWorldPosition();
                if(owp&&owp->guarding) {
                    auto* g=owp->guarding->asNPC();
                    auto [can,gdil]=g->guardtalk(person);
                    if(!can) return gStr("The "+propStr(g->properties,"object")+" stops me.\n\n"+cmdTalk(propStr(g->properties,"object"),gdil).asStr());
                    person->move(targetWP); world=iw; updateIdx(); cls();
                    std::string ts=gdil?cmdTalk(propStr(g->properties,"object"),gdil).asStr():"";
                    return gStr((ts.empty()?"":ts+"\n\n")+"Going \x1b[1m\x1b[38;5;30m"+dir_+"\x1b[0m...\n\n"+cmdLook().asStr());
                }
                person->move(targetWP); world=iw; updateIdx(); cls();
                return gStr("Going \x1b[1m\x1b[38;5;30m"+dir_+"\x1b[0m...\n\n"+cmdLook().asStr());
            } else if(type=="path") {
                auto& pe=aroundpaths.at(propStr(obj->properties,"object"));
                if(pe.pos==person->position->worldposition) return gStr("I am already there!");
                int dx=pe.pos.first==cx?0:(pe.pos.first>cx?1:-1);
                int dy=pe.pos.second==cy?0:(pe.pos.second>cy?1:-1);
                dir_=""; if(dy) { dir_+=dy>0?"north":"south"; if(dx) dir_+="-"; } if(dx) dir_+=dx>0?"east":"west";
                auto* twp=world->positions.at(pe.pos);
                if(twp->guarding){ auto* g=twp->guarding->asNPC(); auto [can,gdil]=g->guardtalk(person);
                    if(!can) return gStr("The "+propStr(g->properties,"object")+" stops me.\n\n"+cmdTalk(propStr(g->properties,"object"),gdil).asStr());
                    person->move(twp); updateIdx(); cls();
                    std::string ts=gdil?cmdTalk(propStr(g->properties,"object"),gdil).asStr():"";
                    return gStr((ts.empty()?"":ts+"\n\n")+"Going \x1b[1m\x1b[38;5;30m"+dir_+"\x1b[0m...\n\n"+cmdLook().asStr()); }
                person->move(twp); updateIdx(); cls();
                return gStr("Going \x1b[1m\x1b[38;5;30m"+dir_+"\x1b[0m...\n\n"+cmdLook().asStr());
            } else {
                // Move towards object
                auto* owp=obj->position->asWorldPosition();
                if(!owp){ if(obj->position==&person->inventory) { std::string ref=propStr(obj->properties,"reference"); return gStr("I'm carrying "+(ref.empty()?"it":ref)+"!"); } return gStr("I can't go there."); }
                auto pos=owp->worldposition, mypos=person->position->worldposition;
                if(pos==mypos) return gStr("I am already there!");
                int dx=pos.first==mypos.first?0:(pos.first>mypos.first?1:-1);
                int dy=pos.second==mypos.second?0:(pos.second>mypos.second?1:-1);
                dir_=""; if(dy){dir_+=dy>0?"north":"south"; if(dx)dir_+="-";} if(dx)dir_+=dx>0?"east":"west";
                auto np=std::make_pair(mypos.first+dx,mypos.second+dy);
                if(np.first<0||np.first>=world->size.first||np.second<0||np.second>=world->size.second) return gStr("I cannot go "+dir_+" from here.");
                auto* twp=world->positions.at(np);
                if(twp->blocking) return gStr(capFirst(anifier(propStr(twp->blocking->properties,"object")))+" blocks my way.");
                if(twp->guarding){ auto* g=twp->guarding->asNPC(); auto [can,gdil]=g->guardtalk(person);
                    if(!can) return gStr("The "+propStr(g->properties,"object")+" stops me.\n\n"+cmdTalk(propStr(g->properties,"object"),gdil).asStr());
                    if(auto* iw2=world->asInsideWorld()) if(iw2->exitfrom.count(twp->worldposition)) return cmdExit();
                    person->move(twp); updateIdx(); cls();
                    std::string ts=gdil?cmdTalk(propStr(g->properties,"object"),gdil).asStr():"";
                    return gStr((ts.empty()?"":ts+"\n\n")+"Going \x1b[1m\x1b[38;5;30m"+dir_+"\x1b[0m...\n\n"+cmdLook().asStr()); }
                if(auto* iw2=world->asInsideWorld()) if(iw2->exitfrom.count(twp->worldposition)) return cmdExit();
                person->move(twp); updateIdx(); cls();
                return gStr("Going \x1b[1m\x1b[38;5;30m"+dir_+"\x1b[0m...\n\n"+cmdLook().asStr());
            }
        } else {
            return gStr("I don't see that here.");
        }

        // Cardinal direction
        if(isCard) {
            if(cardPos.first<0||cardPos.first>=world->size.first||cardPos.second<0||cardPos.second>=world->size.second)
                return gStr("I cannot go "+dir_+" from here.");
            auto* wp=world->positions.at(cardPos);
            if(wp->insideworld) return cmdMove(propStr(wp->insideworld->properties,"object"));
            if(wp->blocking) return gStr(capFirst(anifier(propStr(wp->blocking->properties,"object")))+" blocks my way.");
            if(wp->guarding){ auto* g=wp->guarding->asNPC(); auto [can,gdil]=g->guardtalk(person);
                if(!can) return gStr("The "+propStr(g->properties,"object")+" stops me.\n\n"+cmdTalk(propStr(g->properties,"object"),gdil).asStr());
                if(auto* iw2=world->asInsideWorld()) if(iw2->exitfrom.count(wp->worldposition)) return cmdExit();
                person->move(wp); updateIdx(); cls();
                std::string ts=gdil?cmdTalk(propStr(g->properties,"object"),gdil).asStr():"";
                return gStr((ts.empty()?"":ts+"\n\n")+"Going \x1b[1m\x1b[38;5;30m"+dir_+"\x1b[0m...\n\n"+cmdLook().asStr()); }
            if(auto* iw2=world->asInsideWorld()) if(iw2->exitfrom.count(wp->worldposition)) return cmdExit();
            person->move(wp); updateIdx(); cls();
            return gStr("Going \x1b[1m\x1b[38;5;30m"+dir_+"\x1b[0m...\n\n"+cmdLook().asStr());
        }
        return gStr("I don't see that here.");
    }

    // ── cmdTake ──────────────────────────────────────────────
    GameResult cmdTake(const std::string& inp) {
        if(inp.empty()) return gStr("What should I take?");
        auto res=resolveWithHolder(inp,"from","on","in");
        if(!res.obj) return gStr(res.error);
        auto* obj=res.obj;
        std::string name=propStr(obj->properties,"object");
        if(person->inventory.holding.count(obj)) return gStr(propBool(obj->properties,"plural")?"I already have them!":"I already have it!");
        if(typeOf(obj)=="npc"&&!propBool(obj->properties,"movable")) return gStr("I wouldn't dare try.");
        if(!propBool(obj->properties,"movable")) return gStr("I can't take that.");
        person->take(obj); updateIdx();
        return gStr(res.holdername.empty()?("Took \x1b[1m\x1b[38;5;136m"+name+"\x1b[0m."):("Took \x1b[1m\x1b[38;5;136m"+name+"\x1b[0m from \x1b[1m\x1b[38;5;136m"+res.holdername+"\x1b[0m."));
    }

    // ── cmdPut ───────────────────────────────────────────────
    GameResult cmdPut(const std::string& inp) {
        if(inp.empty()) return gStr("What should I put where?");
        if(inp.find(" on ")==std::string::npos&&inp.find(" in ")==std::string::npos&&inp.find(" into ")==std::string::npos)
            return gStr("Where should I put it?");
        return cmdDrop(inp);
    }

    // ── cmdWear / cmdUnwear ───────────────────────────────────
    GameResult cmdWear(const std::string& inp) {
        if(inp.empty()) return gStr("What should I wear?");
        std::string n=inp; if(n.substr(0,4)=="the ") n=n.substr(4);
        if(!objectindex.count(n)) return gStr("I don't have that.");
        auto* obj=objectindex.at(n);
        if(!person->inventory.holding.count(obj)) return gStr("I don't have that.");
        if(!propBool(obj->properties,"wearable")) return gStr("That's ridiculous.");
        if(person->wearing.count(obj)) return gStr("I'm already wearing it!");
        return obj->wear(person);
    }
    GameResult cmdUnwear(const std::string& inp) {
        if(inp.empty()) return gStr("What should I take off?");
        std::string n=inp; if(n.substr(0,4)=="the ") n=n.substr(4);
        if(!objectindex.count(n)) return gStr("I don't have that.");
        auto* obj=objectindex.at(n);
        if(!person->inventory.holding.count(obj)) return gStr("I don't have that.");
        if(!propBool(obj->properties,"wearable")) return gStr("That's ridiculous.");
        if(!person->wearing.count(obj)) return gStr("I'm not wearing that.");
        return obj->unwear(person);
    }

    // ── cmdDrop ──────────────────────────────────────────────
    GameResult cmdDrop(const std::string& inp) {
        if(inp.empty()) return gStr("What should I drop?");
        std::string on,cn;
        auto trySplit=[&](const std::string& sep)->bool{
            size_t p=inp.find(" "+sep+" ");
            if(p==std::string::npos) return false;
            on=inp.substr(0,p); cn=inp.substr(p+sep.size()+2); return true;
        };
        if(!trySplit("on")&&!trySplit("in")) on=inp;
        if(on.substr(0,4)=="the ") on=on.substr(4);
        if(!cn.empty()&&cn.substr(0,4)=="the ") cn=cn.substr(4);
        if(!objectindex.count(on)) return gStr("I don't have that.");
        auto* obj=objectindex.at(on); on=propStr(obj->properties,"object");
        if(!person->inventory.holding.count(obj)) return gStr("I don't have that.");
        if(person->wearing.count(obj)) return gStr("Remove it first.");

        Container* dest=person->position;
        std::string destOA;
        if(!cn.empty()) {
            if(!objectindex.count(cn)) return gStr("I don't see that here.");
            auto* gdest=objectindex.at(cn);
            if(gdest==obj) return gStr("The \x1b[1m\x1b[38;5;136m"+on+"\x1b[0m warps and drops into itself as the world ends...");
            std::string type=typeOf(gdest);
            if(propHas(gdest->properties,"open")&&!propBool(gdest->properties,"open")) return gStr("It's locked!");
            if(type!="container"&&type!="world-position"&&type!="blocking-container")
                return gStr("I can't do that.");
            if(propHas(gdest->properties,"putinside")&&!propBool(gdest->properties,"putinside"))
                return gStr("I can't do that.");
            destOA=propStr(gdest->properties,"objectsare");
            // Route the dropped item into the destination container's holding set.
            // A thin adapter turns any object's holding set into a Container* target.
            if(auto* hold=gdest->holdingPtr()){
                struct HoldAdapter : Container {
                    std::set<GameObject*>* h;
                    explicit HoldAdapter(std::set<GameObject*>* s):h(s){}
                    void add(GameObject* g) override { h->insert(g); g->position=this; }
                    void remove(GameObject* g) override { h->erase(g); }
                };
                static std::unordered_map<std::set<GameObject*>*,HoldAdapter*> adapters;
                if(!adapters.count(hold)) adapters[hold]=new HoldAdapter(hold);
                person->drop(obj,adapters[hold]); updateIdx();
                return gStr("Dropped \x1b[1m\x1b[38;5;136m"+on+"\x1b[0m "+destOA+" \x1b[1m\x1b[38;5;136m"+cn+"\x1b[0m.");
            }
        }
        person->drop(obj,dest); updateIdx();
        if(cn.empty()) return gStr("Dropped \x1b[1m\x1b[38;5;136m"+on+"\x1b[0m.");
        return gStr("Dropped "+on+" "+destOA+" "+cn+".");
    }

    // ── cmdRide ──────────────────────────────────────────────
    GameResult cmdRide(const std::string& inp) {
        if(inp.empty()) return gStr("What should I ride?");
        std::string n=inp; if(n.substr(0,4)=="the ") n=n.substr(4);
        if(!objectindex.count(n)) return gStr("I don't see that here.");
        auto* obj=objectindex.at(n);
        std::string name=propStr(obj->properties,"object");
        if(propBool(obj->properties,"rideable")) return cmdUse(name);
        return gStr("That's ridiculous.");
    }

    // ── cmdUse ───────────────────────────────────────────────
    GameResult cmdUse(const std::string& inp) {
        if(inp.empty()) return gStr("What should I use?");
        std::string xn,yn;
        size_t p=inp.find(" on ");
        if(p!=std::string::npos){ xn=inp.substr(0,p); yn=inp.substr(p+4); }
        else xn=inp;
        if(xn.substr(0,4)=="the ") xn=xn.substr(4);
        if(!yn.empty()&&yn.substr(0,4)=="the ") yn=yn.substr(4);
        if(!objectindex.count(xn)) return gStr("I don't see that here.");
        if(!yn.empty()&&!objectindex.count(yn)) return gStr("I don't see that here.");
        auto* objx=objectindex.at(xn);
        if(!propBool(objx->properties,"usable")) return gStr("That's ridiculous.");
        GameObject* objy=yn.empty()?nullptr:objectindex.at(yn);
        if(objy) {
            auto* us=propSet(objx->properties,"objectsusableon");
            bool can=us?us->count(yn)>0:true;
            if(!can) return gStr("I can't do that.");
            auto res=objx->use(person,objy); updateIdx(); return res;
        }
        auto res=objx->use(person); updateIdx(); return res;
    }

    // ── parse ────────────────────────────────────────────────
    GameResult parse(const std::string& line_) {
        std::string line=line_;
        while(!line.empty()&&(line.front()==' '||line.front()=='\t')) line=line.substr(1);
        while(!line.empty()&&(line.back()==' '||line.back()=='\t'||line.back()=='\r'||line.back()=='\n')) line.pop_back();
        if(line.empty()) return gStr("Time passes...");
        std::string low=line;
        std::transform(low.begin(),low.end(),low.begin(),::tolower);

        std::vector<std::string> words;
        for(size_t i=0;i<low.size();) {
            while(i<low.size()&&isspace((unsigned char)low[i])) ++i;
            size_t j=i; while(j<low.size()&&!isspace((unsigned char)low[j])) ++j;
            if(j>i) words.push_back(low.substr(i,j-i));
            i=j;
        }

        CmdFn* cmd=nullptr;
        std::vector<std::string> inputs;
        std::string ongoing;

        for(auto& word:words) {
            std::string pot=ongoing.empty()?word:ongoing+" "+word;
            if(ongoingcmds.count(pot)) { ongoing=pot; }
            else if(commands.count(pot)) {
                if(cmd) return gStr("\x1b[31mSorry, I don't understand.\x1b[0m");
                cmd=&commands.at(pot); ongoing.clear();
            } else if(cmd) { inputs.push_back(word); }
            else if(commands.count(ongoing)) {
                inputs.push_back(word); cmd=&commands.at(ongoing); ongoing.clear();
            } else { return gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); }
        }
        if(commands.count(ongoing)) { if(cmd) return gStr("\x1b[31mSorry, I don't understand.\x1b[0m"); cmd=&commands.at(ongoing); }
        if(!cmd) return gStr("\x1b[31mSorry, I don't understand.\x1b[0m");
        std::string inp; for(size_t i=0;i<inputs.size();++i){if(i) inp+=" "; inp+=inputs[i];}
        return (*cmd)(inp);
    }
};

// ─────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────
static Engine* initGame() {
    Engine* engine = new Engine();
    engine->setup();
    return engine;
}

// pyReprQuote(): mirrors Python's repr() quoting for the filename embedded in
// str(OSError) — single-quoted normally, double-quoted if the string contains
// a "'" but no '"', with backslashes/quotes/control chars escaped either way.
static std::string pyReprQuote(const std::string& s) {
    bool hasSingle=false, hasDouble=false;
    for(char c : s) { if(c=='\'') hasSingle=true; if(c=='"') hasDouble=true; }
    char quote = (hasSingle && !hasDouble) ? '"' : '\'';
    std::string r; r.push_back(quote);
    for(unsigned char c : s) {
        if(c=='\\') r += "\\\\";
        else if(c==(unsigned char)quote) { r.push_back('\\'); r.push_back((char)c); }
        else if(c=='\n') r += "\\n";
        else if(c=='\r') r += "\\r";
        else if(c=='\t') r += "\\t";
        else if(c<0x20 || c==0x7f) { char buf[8]; std::snprintf(buf,sizeof(buf),"\\x%02x",c); r += buf; }
        else r.push_back((char)c);
    }
    r.push_back(quote);
    return r;
}

int main(int argc, char** argv) {
    std::signal(SIGINT, [](int){ std::exit(1); });
    std::string fileOpt; bool fileSet=false; bool noColorSet=false; bool noAnsiSet=false; bool loadSet=false;
    if(!parseArgs(argc, argv, fileOpt, fileSet, noColorSet, noAnsiSet, loadSet)) return 1;
    ansiEnabled = !noAnsiSet;
    colorEnabled = ansiEnabled && !noColorSet;
    if(fileSet) {
        logfile = std::fopen(fileOpt.c_str(), "w");
        if(!logfile) {
            std::printf("error in opening file \"%s\": [Errno %d] %s: %s\n",
                        fileOpt.c_str(), errno, std::strerror(errno), pyReprQuote(fileOpt).c_str());
            return 1;
        }
    }
    auto pyStripLower=[](const std::string& s){
        auto isws=[](unsigned char c){ return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f'; };
        size_t a=0,b=s.size();
        while(a<b&&isws((unsigned char)s[a])) ++a;
        while(b>a&&isws((unsigned char)s[b-1])) --b;
        std::string r=s.substr(a,b-a);
        std::transform(r.begin(),r.end(),r.begin(),::tolower);
        return r;
    };
    auto askPlayAgain=[&](bool& restart)->bool{
        std::string pa;
        while(true) {
            pa=getInputWrapped("\x1b[1m\x1b[32mDo you want to play again? (yes/no): \x1b[0m");
            if(gStdinEOF) return false;
            while(!pa.empty()&&(pa.front()=='\r'||pa.front()==' '||pa.front()=='\t')) pa.erase(pa.begin());
            while(!pa.empty()&&(pa.back()=='\r'||pa.back()==' '||pa.back()=='\t')) pa.pop_back();
            std::transform(pa.begin(),pa.end(),pa.begin(),::tolower);
            if(pa=="yes"||pa=="no") break;
            pyprint("That is not a valid option.\n\n");
        }
        if(pa=="yes"){ restart=true; return true; }
        pyprint("\nbye\n"); return true;
    };
    bool firstRound=true;
    while(true) {
        Engine* engine = initGame();
        if(firstRound && loadSet) engine->parse("load");
        firstRound=false;
        bool restart=false;
        while(true) {
            std::string cmd=getInputWrapped("\n> ");
            if(gStdinEOF) return 0;
            pyprint("\n");
            auto out=engine->parse(cmd);
            if(out.isEnd()){
                if(out.eg.description.empty()&&!out.eg.win) { pyprint(out.eg.endmessage+"\n"); return 0; }
                cls();
                pyprint("\x1b[3m"+out.eg.description+"\x1b[0m\n\n"+out.eg.endmessage+"\n\n"+
                     (out.eg.win?"\x1b[1m\x1b[32mYOU WIN!\x1b[0m":"\x1b[1m\x1b[31mYOU LOSE.\x1b[0m")+"\n\n");
                if(!askPlayAgain(restart)) return 0;
                if(restart) break;
                return 0;
            }
            if(out.isStr()&&!out.asStr().empty()) {
                pyprint(out.asStr()+"\n");
                int remain=engine->maxcommands-engine->donecommands-1;
                if(remain>0&&remain<=20) pyprint("\n\n\x1b[31mYou only have "+std::to_string(remain)+" "+(remain!=1?"commands":"command")+" left!\x1b[0m");
                else if(remain==0) pyprint("\n\n\x1b[31mYou have 0 commands left!\x1b[0m");
                std::string tlow=pyStripLower(cmd);
                if(tlow!="moves"&&tlow!="commands"&&out.asStr()!="\x1b[31mSorry, I don't understand.\x1b[0m"&&out.asStr()!="Time passes...") {
                    if(++engine->donecommands==engine->maxcommands) {
                        pyprint("\n\x1b[31mOh no! It is too late. Your rivals have come back to the island and destroyed the device! You have now lost your hard work forever.\x1b[0m\n\n");
                        if(!askPlayAgain(restart)) return 0;
                        if(restart) break;
                        return 0;
                    }
                }
            }
        }
        if(restart) continue;
    }
    return 0;
}
