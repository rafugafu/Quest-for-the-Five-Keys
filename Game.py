#!/usr/bin/python3







"""
__________________________________________________
______________________STOP!_______________________

HAVE YOU PLAYED THE GAME BEFORE READING THE CODE ?
IF NOT, GO PLAY IT FIRST.

...

WHY ARE YOU STILL READING THIS?
CLOSE THE CODE.
GO PLAY THE GAME LIKE A NORMAL PLAYER.
THE CODE WILL STILL BE HERE LATER.

IF YOU HAVE, GO AHEAD!

"""







"""A text-adventure game played in the terminal.

You are a scientist who has to recover the result of your experiment from an
electronic device on a remote island. The device opens only when all five
passwords (see PASSWORDS at the bottom of the file) have been entered, each
exactly once. Any wrong or repeated password destroys it. You have a limited
number of commands (Game.maxcommands) to finish.

Command line options:
    --log-file FILENAME   write a plain-text transcript of the game to FILENAME
    --no-color        strip color/style ANSI codes from the output
    --no-ansi         strip all ANSI codes (implies --no-color)

Overview of the object model:
    * World / InsideWorld are grids of WorldPosition cells. An InsideWorld
      (house, ship, forest, ...) sits on one cell of its parent world and is
      entered and left through that cell.
    * Everything a WorldPosition holds (Object, ContainerObject, NPC, ...) has a
      "properties" dict that drives how Game describes and treats it. Keys:
        type            kind of thing ("object", "container", "npc", "note", ...)
        object          the name the player types and sees
        secondname      an alternative name that also resolves to the thing
        movable         whether it can be taken
        color / material / other / otherafter / height / width
                        adjectives used by examine
        message         extra sentence appended by examine
        reference       pronoun for NPCs ("he", "she") or plurals ("they")
        plural / pluralreference
                        plural things ("some trees") and their determiner
        objectsare      preposition for things held inside it ("on", "in", ...)
        open / key      whether a container is open and which item toggles it
        hiddeninside / putinside
                        hide a container's contents from look / forbid putting
                        things in it
        usable / wearable / rideable / digable
                        which verbs apply
    * NPCs answer talk and give through dialogues() and give(), which return
      dialogue trees (see Game.talk).
    * Game parses typed lines, resolves object names and runs the handlers.
"""

import sys
import signal

# Ctrl+C quits quietly with status 1 instead of showing a traceback.
exit = sys.exit
signal.signal(signal.SIGINT, lambda *_, **__: exit(1))
exitwith = lambda message: [print(message), exit(1)]


def argparse(options, args):
    """Parse command-line arguments against a table of allowed options.
    The value stored for each option in `options` says how it behaves:
        True    option takes a single value (--name value or --name=value)
        False   option is a flag and takes no value
        []      option takes any number of values and may repeat
    Exits with status 1 and an error message on any invalid usage.
    Args:
        options: mapping of option name (without "--") to its kind, see above.
        args: the raw argument strings (normally sys.argv[1:]).
    Returns:
        A dict with the same keys: flags become True/False, single-value options
        become their value (or None when omitted) and list options their values.
    """
    options = {
        key: (item if item != [True] else []) for key, item in options.copy().items()
    }
    exitwith = lambda message: [print(message), exit(1)]
    curarg = None
    for arg in args:
        if not curarg and not arg.startswith("--"):
            exitwith(f'error: unexpected "{arg}"')
        if curarg and options[curarg] in (False, None) and not arg.startswith("--"):
            exitwith(f'error: option "{curarg}" does not take any argument')
        if arg.startswith("--"):
            if curarg and options[curarg] not in (False, None):
                exitwith(f'error: unexpected "{arg}" after option "--{curarg}"')
            arg = arg[2:]
        if curarg and options[curarg] in (False, None):
            curarg = None
        if not curarg:
            if "=" in arg:
                opn, opv = arg.split("=", 1)
                if opn not in options:
                    exitwith(f'error: unknown option "--{opn}"')
                if options[opn] == True:
                    options[opn] = opv
                elif type(options[opn]) == list:
                    options[opn].append(opv)
                elif options[opn] in (False, None):
                    exitwith(f'error: option "--{opn}" does not take any argument')
                else:
                    exitwith(f'error: repeated argument "--{arg}"')
            else:
                if arg not in options:
                    exitwith(f'error: unknown option "--{arg}"')
                if options[arg] == True or type(options[arg]) == list:
                    curarg = arg
                elif options[arg] == False:
                    curarg = arg
                    options[arg] = None
                else:
                    exitwith(f'error: repeated argument "--{arg}"')
        elif curarg:
            if type(options[curarg]) == list:
                options[curarg].append(arg)
            else:
                options[curarg] = arg
            curarg = None
    if curarg and options[curarg] not in (False, None):
        exitwith(f'error: unspecified option "{curarg}"')
    for option in options:
        if options[option] is None:
            options[option] = True
        elif options[option] == True:
            options[option] = None
    return options


# --log-file takes a value; --no-color, --no-ansi, and --load are flags.
options = {
    "log-file": True,
    "no-color": False,
    "no-ansi": False,
    "load": False,
}
args = sys.argv[1:]
options = argparse(options, args)
use_color = not options["no-color"]
use_ansi = not options["no-ansi"]
if not use_ansi:
    use_color = False
# ANSI codes that only change color or style (removed by --no-color).
stripcoloransi = [
    "\x1b[" + code
    for code in (
        "0m",
        "1m",
        "31m",
        "32m",
        "38;5;136m",
        "38;5;30m",
        "3m",
        "4m",
        "1;40;36m",
    )
]
# color codes plus screen-control codes (removed by --no-ansi and in the log file).
stripallansi = stripcoloransi + [
    "\x1b[" + code for code in ("2J", "3J", "H", "?1049h", "?1049l")
]
# Open the transcript file if one was requested.
if options["log-file"] is not None:
    try:
        writefile = open(options["log-file"], "w", encoding="utf-8")
    except Exception as error:
        exitwith(f'error in opening file "{options["log-file"]}": {error}')
else:
    writefile = None


def printoutput(string="", *args, **kwargs):
    """Print a line of game output, adapting it to the --no-color/--no-ansi options.
    The text is also appended to the log file (with every ANSI code removed) when
    one was requested. Extra arguments are passed straight through to print().
    """
    if not use_ansi:
        for ansi in stripallansi:
            string = string.replace(ansi, "")
    elif not use_color:
        for ansi in stripcoloransi:
            string = string.replace(ansi, "")
    if writefile:
        noansistring = string
        for ansi in stripallansi:
            noansistring = noansistring.replace(ansi, "")
        if noansistring:
            writefile.write("=== Game Output ===\n\n" + noansistring + "\n\n")
            writefile.flush()
    return print(string, *args, **kwargs)


def _getinput(string="", *args, **kwargs):
    """Prompt the user like input() while honouring the --no-color/--no-ansi options.
    When logging, both the prompt and the user's reply are recorded in the log file.
    Returns:
        The raw string the user typed.
    """
    if not use_ansi:
        for ansi in stripallansi:
            string = string.replace(ansi, "")
    elif not use_color:
        for ansi in stripcoloransi:
            string = string.replace(ansi, "")
    userinput = input(string, *args, **kwargs)
    if writefile:
        noansiinputstring = string
        for ansi in stripallansi:
            noansiinputstring = noansiinputstring.replace(ansi, "")
        writefile.write(
            noansiinputstring + "\n\n" + "=== User Input ===\n\n" + userinput + "\n\n"
        )
        writefile.flush()
    return userinput

def getinput(string="", *args, **kwargs):
    """Custom getinput function to use queued_inputs list
    and add inputs to list of inputs used for save game. If
    queued input exists, print it as if the user had actually
    typed it."""
    if not queued_inputs:
        queued_inputs.insert(0, _getinput(string, *args, **kwargs))
    else:
        printoutput(string + queued_inputs[0] + "\n")
    gotinput = queued_inputs.pop(0)
    inputs.append(gotinput)
    return gotinput


class WorldPosition:
    """A single cell of a World or InsideWorld grid.
    Tracks what is on the cell (holding), whether something blocks or guards it,
    whether an InsideWorld is attached to it and what should be described to the
    player when they look around from here (properties["points"]).
    """

    def __init__(self, world, position):
        """Args:
        world: the World or InsideWorld this cell belongs to.
        position: the (x, y) coordinates of this cell in that world.
        """
        self.world = world
        self.worldposition = position
        self.properties = {
            "type": "world-position",
            "points": {
                ("On my " if dir_ else "Right next to me") + dir_: {}
                for dir_ in (
                    "",
                    "north",
                    "south",
                    "east",
                    "west",
                    "north-east",
                    "north-west",
                    "south-east",
                    "south-west",
                )
            },
            "skip": set(),
        }
        self.holding = set()
        self.blocking = None
        self.guarding = None
        self.insideworld = None

    def add(self, object_):
        """Put an object on this cell."""
        self.holding.add(object_)

    def remove(self, object_):
        """Take an object off this cell."""
        self.holding.remove(object_)

    def block(self, object_):
        """Mark this cell as blocked by an object, so nobody can walk onto it."""
        self.blocking = object_

    def unblock(self):
        """Clear whatever is blocking this cell."""
        self.blocking = None

    def guard(self, object_):
        """Set an NPC guarding this cell, who gets to react when somebody tries to enter it."""
        self.guarding = object_

    def unguard(self):
        """Remove the guard from this cell."""
        self.guarding = None

    def addinworld(self, object_):
        """Record the InsideWorld (house, ship, ...) that is entered through this cell."""
        self.insideworld = object_

    def removeinworld(self, object_):
        """Forget the InsideWorld attached to this cell."""
        self.insideworld = None

    def point(self, highlightname, thing, plural, dir_, skip=None):
        """Add something the player can see from this cell without it being an object here.
        Args:
            highlightname: the highlighted word shown in the description.
            thing: the phrase that follows it ("to the forest"), or None.
            plural: whether it should be described as plural.
            dir_: direction relative to the player ("north", ...), or None for "right
                next to me".
            skip: the object name to leave out of the normal object listing (defaults
                to highlightname).
        """
        if skip is None:
            skip = highlightname
        if dir_:
            dir_ = "On my " + dir_
        else:
            dir_ = "Right next to me"
        self.properties["points"][dir_][highlightname] = (thing, plural)
        self.properties["skip"].add(skip)


class World:
    """The outdoor map: a rectangular grid of WorldPositions plus the paths on it."""

    def __init__(self, size):
        """Args:
        size: (width, height) of the grid; a WorldPosition is created for each cell.
        """
        self.positions = {}
        self.size = size
        self.paths = set()
        self.properties = {"type": "world"}
        for x in range(self.size[0]):
            for y in range(self.size[1]):
                self.positions[(x, y)] = WorldPosition(self, (x, y))

    def inside(self, position, place, insidereference, pluralreference=None):
        """Give a cell a description of what the player is "in" while standing on it.
        Args:
            position: (x, y) of the cell.
            place: the place name ("village").
            insidereference: the preposition to use ("in", "inside", ...).
            pluralreference: optional determiner to use instead of "a"/"an" ("the").
        """
        self.positions[position].properties["inside"] = place
        self.positions[position].properties["insidereference"] = insidereference
        if pluralreference is not None:
            self.positions[position].properties["pluralreference"] = pluralreference

    def pointall(self, position, highlightname, thing, plural, skip=None):
        """Make a thing visible from a cell and its neighbours through WorldPosition.point.
        Args:
            position: (x, y) of the cell the thing is at.
            highlightname: the highlighted word used in descriptions.
            thing: phrase following the highlighted word, or None.
            plural: whether it is described as plural.
            skip: optional object name to hide from the normal listing.
        """
        dirnames = {
            (0, 0): None,
            (0, 1): "south",
            (0, -1): "north",
            (1, 0): "west",
            (-1, 0): "east",
            (1, 1): "south-west",
            (-1, 1): "south-east",
            (1, -1): "north-west",
            (-1, -1): "north-east",
        }
        for xi in range(-1, 2):
            for yi in range(-1, 2):
                dir_ = dirnames[(xi, yi)]
                x = position[0] + xi
                y = position[1] + yi
                if (x, y) not in self.positions:
                    continue
                self.positions[(x, y)].point(highlightname, thing, plural, dir_, skip)


class InsideWorld:
    """A smaller grid that is nested inside a cell of another world (a house, a ship, ...).
    It behaves like a World for movement and description, and like a normal object
    for its parent: it can be examined, entered and (rarely) moved or deleted.
    """

    def __init__(
        self,
        position,
        size,
        exitpos,
        startingpos,
        exitablepositions=None,
        exitfrom=None,
    ):
        """Args:
        position: the parent WorldPosition this place sits on.
        size: (width, height) of the inner grid.
        exitpos: the parent WorldPosition the player ends up on when leaving.
        startingpos: (x, y) inside this world where the player arrives.
        exitablepositions: inner cells from which "exit" is allowed. Either an
            explicit collection, "all", or "visible" (cells near exitfrom).
            An empty collection means every cell allows exiting.
        exitfrom: inner cells that automatically lead outside when walked onto.
        """
        if exitablepositions is None:
            exitablepositions = set()
        if exitfrom is None:
            exitfrom = set()
        self.position = position
        self.size = size
        self.paths = set()
        self.blocking = None
        self.guarding = None
        self.exitfrom = exitfrom
        self.properties = {"type": "inside-world"}
        self.positions = {}
        for x in range(self.size[0]):
            for y in range(self.size[1]):
                self.positions[(x, y)] = WorldPosition(self, (x, y))
        if exitablepositions == "all":
            self.exitablepositions = self.positions.keys()
        elif exitablepositions == "visible":
            self.exitablepositions = set()
            for ep in self.exitfrom:
                self.exitablepositions.update(
                    {
                        (x, y)
                        for x in range(ep[0] - 1, ep[0] + 2)
                        for y in range(ep[1] - 1, ep[1] + 2)
                        if (x, y) in self.positions
                    }
                )
        else:
            self.exitablepositions = exitablepositions
        self.startingpos = self.positions[startingpos]
        self.exitposition = exitpos
        self.position.add(self)
        self.position.addinworld(self)

    def inside(self, position, place, insidereference, pluralreference=None):
        """Give an inner cell a description of what the player is "in" while standing on it.
        Same arguments as World.inside.
        """
        self.positions[position].properties["inside"] = place
        self.positions[position].properties["insidereference"] = insidereference
        if pluralreference is not None:
            self.positions[position].properties["pluralreference"] = pluralreference

    def pointall(self, position, highlightname, thing, plural, skip=None):
        """Make a thing visible from an inner cell and its neighbours through WorldPosition.point.
        Same arguments as World.pointall.
        """
        dirnames = {
            (0, 0): None,
            (0, 1): "south",
            (0, -1): "north",
            (1, 0): "west",
            (-1, 0): "east",
            (1, 1): "south-west",
            (-1, 1): "south-east",
            (1, -1): "north-west",
            (-1, -1): "north-east",
        }
        for xi in range(-1, 2):
            for yi in range(-1, 2):
                dir_ = dirnames[(xi, yi)]
                x = position[0] + xi
                y = position[1] + yi
                if (x, y) not in self.positions:
                    continue
                self.positions[(x, y)].point(highlightname, thing, plural, dir_, skip)

    def block(self, object_):
        """Mark this place as blocked by an object."""
        self.blocking = object_

    def unblock(self):
        """Clear whatever is blocking this place."""
        self.blocking = None

    def guard(self, object_):
        """Set an NPC guarding the entrance of this place."""
        self.guarding = object_

    def unguard(self):
        """Remove the guard from the entrance of this place."""
        self.guarding = None

    def move(self, position):
        """Move this whole place onto another parent cell."""
        self.position.removeinworld()
        self.position.remove(self)
        self.position = position
        self.position.add(self)
        self.position.addinworld(self)

    def delete(self):
        """Remove this place from its parent cell."""
        self.position.removeinworld()
        self.position.remove(self)


class EndGame:
    """Returned by a command or a dialogue to finish the current round.
    Attributes:
        description: text shown first (in italics) describing what happened, or
            None to end silently.
        endmessage: the closing line shown after the description.
        win: True for a win, False for a loss (None when the player quit).
    """

    def __init__(self, description, endmessage, win):
        self.description = description
        self.endmessage = endmessage
        self.win = win


class Object:
    """A plain object lying on a WorldPosition or inside a container."""

    def __init__(self, position):
        """Args:
        position: the WorldPosition or container that holds this object.
        """
        self.position = position
        self.properties = {"type": "object"}
        self.position.add(self)

    def move(self, position):
        """Move this object to another position or container."""
        self.position.remove(self)
        self.position = position
        self.position.add(self)

    def delete(self):
        """Remove this object from the game."""
        self.position.remove(self)


class ContainerObject:
    """An object that can hold other objects (table, box, chest, ...)."""

    def __init__(self, position):
        """Args:
        position: the WorldPosition or container that holds this container.
        """
        self.position = position
        self.properties = {"type": "container"}
        self.holding = set()
        self.position.add(self)

    def add(self, object_):
        """Put an object inside this container."""
        self.holding.add(object_)

    def remove(self, object_):
        """Take an object out of this container."""
        self.holding.remove(object_)

    def move(self, position):
        """Move this container (and its contents) to another position or container."""
        self.position.remove(self)
        self.position = position
        self.position.add(self)

    def delete(self):
        """Remove this container from the game."""
        self.position.remove(self)


class BlockingObject:
    """An object that stops the player from walking onto its cell (wall, ditch, ...)."""

    def __init__(self, position):
        """Args:
        position: the WorldPosition to occupy and block.
        """
        self.position = position
        self.properties = {"type": "blocking-object"}
        self.position.add(self)
        self.position.block(self)

    def move(self, position):
        """Move this object, unblocking its old cell and blocking the new one."""
        self.position.unblock()
        self.position.remove(self)
        self.position = position
        self.position.add(self)
        self.position.block(self)

    def delete(self):
        """Remove this object and unblock its cell."""
        self.position.unblock()
        self.position.remove(self)


class BlockingContainerObject:
    """A blocking object that can also hold other objects."""

    def __init__(self, position):
        """Args:
        position: the WorldPosition to occupy and block.
        """
        self.position = position
        self.properties = {"type": "blocking-container"}
        self.holding = set()
        self.position.add(self)
        self.position.block(self)

    def add(self, object_):
        """Put an object inside this container."""
        self.holding.add(object_)

    def remove(self, object_):
        """Take an object out of this container."""
        self.holding.remove(object_)

    def move(self, position):
        """Move this object, unblocking its old cell and blocking the new one."""
        self.position.unblock()
        self.position.remove(self)
        self.position = position
        self.position.add(self)
        self.position.block(self)

    def delete(self):
        """Remove this object and unblock its cell."""
        self.position.unblock()
        self.position.remove(self)


class Note:
    """Something with text on it that the player can read (paper, book, sticky note, ...)."""

    def __init__(self, position, text):
        """Args:
        position: the WorldPosition or container that holds the note.
        text: what the player sees when reading it.
        """
        self.position = position
        self.properties = {"type": "note"}
        self.text = text
        self.position.add(self)

    def move(self, position):
        """Move this note to another position or container."""
        self.position.remove(self)
        self.position = position
        self.position.add(self)

    def delete(self):
        """Remove this note from the game."""
        self.position.remove(self)

    def read(self):
        """Return the text of the note."""
        return self.text

    def write(self, text):
        """Replace the text of the note."""
        self.text = text


class NPC:
    """A non-player character. Subclasses add dialogues() and give() to react to the player."""

    def __init__(self, position):
        """Args:
        position: the WorldPosition the NPC stands on.
        """
        self.position = position
        self.properties = {"type": "npc", "movable": False}
        self.position.add(self)

    def move(self, position):
        """Move this NPC to another position."""
        self.position.remove(self)
        self.position = position
        self.position.add(self)

    def delete(self):
        """Remove this NPC from the game."""
        self.position.remove(self)


class Path:
    """A straight line of cells on a world that the player can follow (a trail, a hallway).
    Paths are not held by cells; the world keeps them in its "paths" set and Game
    describes them as continuing in two directions.
    """

    def __init__(self, world, a, b):
        """Args:
        world: the world the path is on.
        a: (x, y) of one end of the path.
        b: (x, y) of the other end (horizontal, vertical or diagonal from a).
        """
        self.world = world
        self.a = a
        self.b = b
        self.properties = {"type": "path", "movable": False}
        # Step direction from a to b -> (direction going from a to b, direction going from b to a).
        dirtonames = {
            (0, 1): ("south", "north"),
            (0, -1): ("north", "south"),
            (-1, 0): ("east", "west"),
            (1, 0): ("west", "east"),
            (-1, -1): ("north-east", "south-west"),
            (1, 1): ("south-west", "north-east"),
            (-1, 1): ("south-east", "north-west"),
            (1, -1): ("north-west", "south-east"),
        }
        # Sign of the step (-1, 0 or 1) along each axis.
        diffx, diffy = abs(b[0] - a[0]) / (b[0] - a[0]) if a[0] != b[0] else 0, (
            abs(b[1] - a[1]) / (b[1] - a[1]) if a[1] != b[1] else 0
        )
        self.direction = dirtonames[(diffx, diffy)]
        self.world.paths.add(self)


class StartingPath(Path):
    """The dirt trail leading east from the starting area."""

    def __init__(self, world, a, b):
        super().__init__(world, a, b)
        self.properties.update(
            {
                "object": "dirt trail",
                "color": "brown",
                "other": "narrow,",
                "secondname": "trail",
                "insidereference": "on",
            }
        )


class StartingHorse(NPC):
    """The horse that carries the player over the ditch, once it has been fed an apple.
    Using it swaps the player and horse with jumpposition, so riding it a second
    time takes the player back.
    """

    def __init__(self, position, jumpposition, returnmessage):
        """Args:
        position: where the horse starts.
        jumpposition: the cell on the other side of the ditch.
        returnmessage: what the player sees after riding.
        """
        super().__init__(position)
        self.jumpposition = jumpposition
        self.returnmessage = returnmessage
        self.properties.update(
            {
                "movable": False,
                "object": "horse",
                "color": "brown",
                "other": "big",
                "usable": True,
                "rideable": True,
            }
        )
        self.applegiven = False

    def dialogues(self, person):
        """Reaction to being talked to; the horse cannot see an invisible player."""
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return [
                "The horse looks around, confused, almost like it can't see me.",
                None,
            ]
        return ["Harrumph!", None]

    def give(self, object_, person):
        """Accept an apple (which unlocks riding) and refuse everything else.
        Returns:
            A tuple as described in Game.give.
        """
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return (
                False,
                [
                    "The horse looks around, confused, almost like it can't see me.",
                    None,
                ],
            )
        if object_.properties["object"] != "apple":
            return (False,)
        else:
            self.applegiven = True
            return (True, ["Harrumph! The horse happily eats the apple.", None])

    def use(self, person):
        """Ride the horse to the other side of the ditch (or back).
        Returns:
            The message to show the player.
        """
        if person.position != self.position:
            return "The horse is too far away."
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return "The horse looks around, confused, almost like it can't see me."
        if not self.applegiven:
            return "The horse throws me off."
        oldposition = self.position
        person.move(self.jumpposition)
        self.move(self.jumpposition)
        self.jumpposition = oldposition
        return self.returnmessage


class Watchman(NPC):
    """The guard of the archway leading out of the village.
    Only an invisible player can slip past. Anybody else is stopped, or shoved back
    to outposition when they are found on the wrong side.
    """

    def __init__(self, object_, outposition):
        """Args:
        object_: the thing being guarded (the archway); the watchman stands on it.
        outposition: the WorldPosition the player is shoved back to.
        """
        super().__init__(object_.position)
        self.object_ = object_
        self.outposition = outposition
        self.position.guard(self)
        self.properties.update(
            {
                "movable": False,
                "object": "watchman",
                "other": "big, strong",
                "reference": "he",
                "message": f'He is guarding the \x1b[1m\x1b[38;5;136m{self.object_.properties["object"]}\x1b[0m.',
            }
        )

    def dialogues(self, person):
        """Reaction to being talked to, depending on where the player is standing."""
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return [
                "The watchman looks around, confused, almost like he can't see me.\n\nWhere are you?",
                None,
            ]
        if person.position.worldposition[0] > 6:
            return ["What do you want? You can't go in.", None]
        else:
            person.move(self.outposition)
            return ["How did you get in?\n\nThe watchman shoves me back out.", None]

    def guardtalk(self, person):
        """Called when the player tries to walk onto the guarded cell.
        Returns:
            (allowed, dialogue): whether the player may pass, and what is said.
        """
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return (
                True,
                [
                    "I slip past the \x1b[1m\x1b[38;5;136mwatchman\x1b[0m, invisible.",
                    None,
                ],
            )
        if person.position.worldposition[0] > 6:
            return (False, ["Where do you think you're going?", None])
        else:
            person.move(self.outposition)
            return (
                False,
                ["How did you get in?\n\nThe watchman shoves me back out.", None],
            )

    def give(self, object_, person):
        """The watchman refuses every gift."""
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return (
                False,
                [
                    "The watchman looks around, confused, almost like he can't see me.\n\nWhere are you?",
                    None,
                ],
            )
        return (False,)


class OldLady(NPC):
    """An old lady looking for her son.
    Telling her the son is in the house ends the game. Giving her the card from the
    ship gets the player a folder with the fourth password in return.
    """

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {
                "movable": False,
                "object": "old lady",
                "other": "stern",
                "reference": "she",
                "message": "She is looking around for someone.",
                "secondname": "lady",
            }
        )
        self.done = False

    def dialogues(self, person):
        """Ask about her son, or greet the player again once she has got what she wanted."""
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return [
                "The old lady looks around, confused, almost like she can't see me.\n\nWhere are you?",
                None,
            ]
        if self.done:
            return ["Hello again.", None]
        return [
            "Have you seen my son anywhere?",
            {
                "Yes, he's inside that house": [
                    EndGame(
                        "The old lady goes into the house with me.\n\nYOU DARE SUGGEST MY SON IS THIS... LUNATIC?\n\nWith a sudden, powerful swing, the old lady's wooden stick connects with the side of my head. I stumble blindly, trying desperately to dodge, but I lose my footing and fall hard against the stones. The world spins, and everything goes black.",
                        "A nice way to die, getting hit by an old lady.",
                        False,
                    ),
                    None,
                ],
                "No": ["Ok, tell me if you do, will you?", None],
            },
        ]

    def dodone(self, thing):
        """Remember that the quest is over and pass the thing through.
        Used inside a dialogue tuple so the flag is set exactly when the reward is given.
        """
        self.done = True
        return thing

    def give(self, object_, person):
        """Accept the ship's card and reward the player with a folder holding a sticky note password."""
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return (
                False,
                [
                    "The old lady looks around, confused, almost like she can't see me.\n\nWhere are you?",
                    None,
                ],
            )
        if object_.properties["object"] != "card":
            return (False,)
        folder = PasswdFolder(person.inventory)
        PasswdStickyNote(folder, PASSWORDS[3])
        return (
            True,
            [
                "The old lady reads the note.\nOh! I have been looking around everywhere for him. So he's already on the ship.",
                {
                    "Yes": [
                        "Oh no, he has forgotten this! Can you take it to him?",
                        {
                            "Ok!": ["Thank you!", (self.dodone(folder),)],
                        },
                    ]
                },
            ],
        )


class JokeMan(NPC):
    """A silly man inside the house who cycles through jokes and trades a password for baby food.
    Talking to him the first time also makes the old lady appear.
    """

    def __init__(self, position, talk, oldladyposition):
        """Args:
        position: where the man stands.
        talk: the lines he cycles through (the last entry is never used).
        oldladyposition: where the old lady appears after the first conversation.
        """
        super().__init__(position)
        self.talk = talk
        self.oldlady = None
        self.oldladyposition = oldladyposition
        self.index = -1
        self.properties.update(
            {
                "movable": False,
                "object": "man",
                "other": "funny little",
                "reference": "he",
                "message": "He is wearing a disturbing pink t-shirt with yellow polka dots on it.\nTry talking to him.",
            }
        )

    def dialogues(self, person):
        """Say the next line from the talk list, creating the old lady the first time."""
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return ["Ooooh! Playing hide and seek, I like it! Where are you?", None]
        if not self.oldlady:
            self.oldlady = OldLady(self.oldladyposition)
        self.index = (self.index + 1) % (len(self.talk) - 1)
        return [self.talk[self.index], None]

    def give(self, object_, person):
        """Accept baby food and hand over a paper with the third password."""
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return (
                False,
                ["Flying object in the air, oh flying object in the air!", None],
            )
        if object_.properties["object"] == "baby food":
            return (
                True,
                [
                    "Thank you!!! I love baby food.",
                    {
                        "Ok, no problem!": [
                            "You know, once some people came to this island in a big flying thing!\nThey gave me something and said don't show it or give it to anyone except for them.\nBut because you gave me this amazing food, I will give it to you!",
                            (CodePaper(person.inventory, PASSWORDS[2]),),
                        ],
                        "Shut up and go away you unhinged little loser": [
                            "Waaaaaa I thought you were my friend go out of my house I'll not give it to you.",
                            None,
                        ],
                    },
                ],
            )
        else:
            return (False,)


class StartingMan(NPC):
    """The locksmith who opens the locked box in exchange for a coin.
    Hands out a Key that fits the box.
    """

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {
                "movable": False,
                "object": "man",
                "other": "big, strong",
                "reference": "he",
            }
        )
        self.coingiven = False
        self.done = False

    def dodone(self, thing):
        """Remember that the key has been handed out and pass the thing through.
        Used inside a dialogue tuple so the flag is set exactly when the key is given.
        """
        self.done = True
        return thing

    def dialogues(self, person):
        """Talk to the locksmith; the conversation depends on whether the player has the box and a coin."""
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return [
                "The man looks around, confused, almost like he can't see me.\n\nWhere are you?",
                None,
            ]
        if self.done:
            return ["Thanks for the coin.", None]
        objs = {obj.properties["object"]: obj for obj in person.inventory.holding}
        if "locked box" not in objs:
            if self.coingiven:
                return [
                    "Thanks for the coin. Come back if you ever need to open a lock.",
                    None,
                ]
            return [
                "Hello.",
                {
                    "Hi": [
                        "I was once the greatest locksmith and metalworker in this place... What do you want?",
                        {"Nothing.": ["Come back later then.", None]},
                    ]
                },
            ]
        if self.coingiven:
            return [
                "Do you want to open that box?",
                {
                    "Yes": [
                        "Here is a key to open the box.",
                        (self.dodone(Key(person.inventory, objs["locked box"])),),
                    ]
                },
            ]
        if "coin" in objs:
            objs["coin"].delete()
            self.coingiven = True
            return [
                "Hello.",
                {
                    "Hi": [
                        "You want to open that box for a coin?",
                        {
                            "Yes": [
                                "Thanks. Here is a key for the box.",
                                (
                                    self.dodone(
                                        Key(person.inventory, objs["locked box"])
                                    ),
                                ),
                            ]
                        },
                    ]
                },
            ]
        else:
            return [
                "Hello.",
                {
                    "Hi": [
                        "What do you want?",
                        {
                            "Can you open this box for me?": [
                                "Show me the box",
                                {
                                    "Give him the box": [
                                        "He looks at the box carefully and gives it back.\nI can if you have a coin...",
                                        None,
                                    ],
                                    "Don't give him the box": ["Well, bye then.", None],
                                },
                            ],
                            "Nothing.": ["Go away then.", None],
                        },
                    ]
                },
            ]

    def give(self, object_, person):
        """Accept a coin or a locked box (taking a coin from the inventory when needed) and offer the key."""
        if "invisible" in person.properties and person.properties["invisible"] == True:
            return (
                False,
                [
                    "The man looks around, confused, almost like he can't see me.\n\nWhere are you?",
                    None,
                ],
            )
        if self.done:
            return (False,)
        objs = {obj.properties["object"]: obj for obj in person.inventory.holding}
        if self.coingiven and object_.properties["object"] == "locked box":
            return (
                False,
                [
                    "Do you want to open that box?",
                    {
                        "Yes": [
                            "Here is a key to open the box.",
                            (self.dodone(Key(person.inventory, object_)),),
                        ]
                    },
                ],
            )
        if "coin" in objs and object_.properties["object"] == "locked box":
            self.coingiven = True
            objs["coin"].delete()
            return (
                False,
                [
                    "Do you want to open that box for a coin?",
                    {
                        "Yes": [
                            "Here is a key to open the box.",
                            (self.dodone(Key(person.inventory, object_)),),
                        ]
                    },
                ],
            )
        if object_.properties["object"] != "coin":
            return (False,)
        self.coingiven = True
        if "locked box" in objs:
            dialog = [
                "Thanks for the coin. Do you want to open that box?",
                {
                    "Yes": [
                        "Here is a key to open the box.",
                        (self.dodone(Key(person.inventory, objs["locked box"])),),
                    ]
                },
            ]
        else:
            dialog = [
                "Thanks for the coin. If you ever want to open a lock, come to me.",
                {"Ok": ["Go now.", None]},
            ]
        return (True, dialog)


class StartingDitch(BlockingObject):
    """The wide ditch that cannot be crossed on foot."""

    def __init__(self, position, width):
        """Args:
        position: the cell it blocks.
        width: width in meters shown by examine.
        """
        super().__init__(position)
        self.properties.update(
            {
                "movable": False,
                "object": "ditch",
                "color": "brown",
                "other": "dirty",
                "width": width,
                "message": "It is too wide to jump across.",
            }
        )


class NormalWall(BlockingObject):
    """A plain stone wall that blocks a cell."""

    def __init__(self, position, height):
        """Args:
        position: the cell it blocks.
        height: height in meters; walls above 3 meters are also called "tall".
        """
        super().__init__(position)
        self.properties.update(
            {
                "movable": False,
                "object": "wall",
                "color": "white",
                "material": "stone",
                "height": height,
            }
        )
        if height > 3:
            self.properties["other"] = "tall"


class ContainerWall(BlockingContainerObject):
    """A stone wall with a hidden gap between its blocks where objects are wedged."""

    def __init__(self, position, height):
        """Args:
        position: the cell it blocks.
        height: height in meters; walls above 3 meters are also called "tall".
        """
        super().__init__(position)
        self.properties.update(
            {
                "movable": False,
                "object": "wall",
                "color": "white",
                "material": "stone",
                "height": height,
                "objectsare": "wedged between some stone blocks in",
                "hiddeninside": True,
                "putinside": False,
            }
        )
        if height > 3:
            self.properties["other"] = "tall"


class House(InsideWorld):
    """The small red house with a bedroom and a kitchen inside."""

    class BedRoom(InsideWorld):
        """The bedroom, containing a bed and a desk with a diary."""

        class Bed(ContainerObject):
            """A single bed with a colorful sheet."""

            class Sheet(Object):
                """The sheet on the bed."""

                def __init__(self, position):
                    super().__init__(position)
                    self.properties.update(
                        {
                            "movable": False,
                            "object": "sheet",
                            "other": "colorful peppa-pig",
                        }
                    )

            def __init__(self, position):
                super().__init__(position)
                self.properties.update(
                    {
                        "movable": False,
                        "object": "bed",
                        "other": "single",
                        "objectsare": "on",
                        "height": 0.3,
                    }
                )
                self.Sheet(self)

        class Desk(ContainerObject):
            """A low wooden desk holding a diary."""

            class Book(Note):
                """The diary, which reveals the second password."""

                def __init__(self, position, text):
                    super().__init__(position, text)
                    self.properties.update(
                        {
                            "movable": True,
                            "object": "book",
                            "other": "black hardcover",
                            "secondname": "diary",
                        }
                    )

            def __init__(self, position):
                super().__init__(position)
                self.properties.update(
                    {
                        "movable": False,
                        "object": "desk",
                        "other": "low",
                        "material": "wood",
                        "objectsare": "on",
                    }
                )
                self.Book(
                    self,
                    f'It seems to be a diary.\nI go through the pages... and find something!\n\n...\nToday some strange people came here with a device and hid it somewhere on the island.\nI heard them talking about keeping some "passwords" hidden so they could find and open the device themselves.\nI heard one of the passwords!\nI wonder what it\'s for... "{PASSWORDS[1]}" it was.\n...',
                )

        def __init__(self, position, exitpos, startingpos=(0, 0)):
            """Args:
            position: the cell of the house that this room sits on.
            exitpos: the cell of the house the player returns to when leaving.
            startingpos: where the player arrives inside the room.
            """
            super().__init__(position, (1, 2), exitpos, startingpos)
            for pos in self.positions:
                self.inside(pos, "bedroom inside a house", "in")
            self.properties.update(
                {"movable": False, "object": "bedroom", "insidereference": "inside"}
            )
            self.Bed(self.positions[(0, 0)])
            self.Desk(self.positions[(0, 1)])

    class Kitchen(InsideWorld):
        """The kitchen, containing a marble platform."""

        class Platform(ContainerObject):
            """A low marble platform."""

            def __init__(self, position):
                super().__init__(position)
                self.properties.update(
                    {
                        "movable": False,
                        "object": "platform",
                        "other": "low",
                        "material": "marble",
                    }
                )

        def __init__(self, position, exitpos, startingpos=(0, 0)):
            """Args:
            position: the cell of the house that this room sits on.
            exitpos: the cell of the house the player returns to when leaving.
            startingpos: where the player arrives inside the room.
            """
            super().__init__(position, (1, 2), exitpos, startingpos)
            for pos in self.positions:
                self.inside(pos, "kitchen inside a house", "in")
            self.properties.update(
                {"movable": False, "object": "kitchen", "insidereference": "inside"}
            )
            self.Platform(self.positions[(0, 1)])

    def __init__(self, position, exitpos):
        """Args:
        position: the world cell the house stands on.
        exitpos: the world cell the player ends up on when leaving the house.
        """
        super().__init__(position, (2, 2), exitpos, (1, 0))
        for pos in self.positions:
            self.inside(pos, "house", "inside")
        self.properties.update(
            {
                "movable": False,
                "object": "house",
                "color": "red",
                "material": "wooden planks",
                "other": "small",
                "insidereference": "inside",
            }
        )
        self.BedRoom(self.positions[(1, 1)], self.positions[(1, 0)])
        self.Kitchen(self.positions[(0, 0)], self.positions[(1, 0)])


class Ship(InsideWorld):
    """The big steel ship, with a cabin holding a note room and the good man's room."""

    class Cabin(InsideWorld):
        """The ship's cabin: a long hallway with two rooms leading off it."""

        class Hallway(Path):
            """The long hallway running through the cabin."""

            def __init__(self, world, a, b):
                super().__init__(world, a, b)
                self.properties.update(
                    {
                        "object": "long hallway",
                        "secondname": "hallway",
                        "insidereference": "in",
                    }
                )

        class ShipNoteRoom(InsideWorld):
            """A small dark room with a table holding the card for the old lady."""

            class ShipNote(Note):
                """The folded card that tells the old lady her son is on the ship."""

                def __init__(self, position, text):
                    super().__init__(position, text)
                    self.properties.update(
                        {
                            "movable": True,
                            "object": "card",
                            "color": "white",
                            "material": "a thin paper",
                            "other": "folded",
                            "message": "Something is written on the card.",
                        }
                    )

            class ShipTable(ContainerObject):
                """The table in the note room."""

                def __init__(self, position):
                    super().__init__(position)
                    self.properties.update(
                        {
                            "movable": False,
                            "object": "table",
                            "other": "low, wooden",
                            "message": "It looks like someone just got up from the table.",
                            "objectsare": "on",
                        }
                    )

            def __init__(self, position, exitpos):
                super().__init__(position, (2, 1), exitpos, (0, 0))
                for pos in self.positions:
                    self.inside(pos, "room inside a ship", "in")
                self.properties.update(
                    {
                        "movable": False,
                        "object": "room",
                        "other": "small, dark",
                        "insidereference": "in",
                    }
                )
                self.ShipNote(
                    self.ShipTable(self.positions[(0, 0)]),
                    "It is a note:\n\nHello,\nI'm on the ship. I'll come back today night. Don't look for me.",
                )

        class ManRoom(InsideWorld):
            """A large room with a cheerful man in a uniform."""

            class GoodMan(NPC):
                """The cheerful man in the ship's room."""

                def __init__(self, position):
                    super().__init__(position)
                    self.properties.update(
                        {
                            "movable": False,
                            "object": "man",
                            "other": "tall, cheerful looking",
                            "otherafter": "wearing a uniform",
                            "reference": "he",
                        }
                    )
                    self.talked = False

                def dialogues(self, person):
                    """Greet the player; a shorter greeting after the first conversation."""
                    if (
                        "invisible" in person.properties
                        and person.properties["invisible"] == True
                    ):
                        return [
                            "The man looks around, confused, almost like he can't see me.\n\nWhere are you?",
                            None,
                        ]
                    if self.talked:
                        return ["Hello again!", None]
                    self.talked = True
                    return [
                        "Hello! Are you coming on the ship?",
                        {
                            "Yes": ["Bye then, see you later!", None],
                            "No": ["Oh, it's fine.", None],
                        },
                    ]

                def give(self, object_, person):
                    """Refuse everything; the folder and sticky note prompt a short explanation."""
                    if (
                        "invisible" in person.properties
                        and person.properties["invisible"] == True
                    ):
                        return (
                            False,
                            [
                                "The man looks around, confused, almost like he can't see me.\nWhere are you?",
                                None,
                            ],
                        )
                    if object_.properties["object"] in ("folder", "sticky note"):
                        return (
                            False,
                            [
                                "Where did you get this?",
                                {
                                    "An old lady gave it to me": [
                                        "Well, I already took another copy.",
                                        None,
                                    ]
                                },
                            ],
                        )
                    return (False,)

            def __init__(self, position, exitpos):
                super().__init__(position, (1, 1), exitpos, (0, 0))
                self.inside((0, 0), "room inside a ship", "in")
                self.properties.update(
                    {
                        "movable": False,
                        "object": "room",
                        "other": "large",
                        "insidereference": "in",
                    }
                )
                self.GoodMan(self.positions[(0, 0)])

        def __init__(self, position, exitpos):
            super().__init__(position, (2, 5), exitpos, (0, 1))
            for pos in self.positions:
                self.inside(pos, "cabin of a ship", "in", "the")
            self.properties.update(
                {
                    "movable": False,
                    "object": "cabin",
                    "material": "mostly of wood",
                    "other": "small",
                    "insidereference": "in",
                    "materialnoof": True,
                }
            )
            self.Hallway(self, (0, 0), (0, 3))
            self.ManRoom(self.positions[(1, 2)], self.positions[(0, 2)])
            self.ShipNoteRoom(self.positions[(0, 4)], self.positions[(0, 3)])

    def __init__(self, position, exitpos):
        super().__init__(position, (3, 1), exitpos, (1, 0), exitfrom={(0, 0)})
        for pos in self.positions:
            self.inside(pos, "deck of a ship", "on", "the")
        self.properties.update(
            {
                "movable": False,
                "object": "ship",
                "color": "black",
                "material": "mostly of steel",
                "other": "big",
                "insidereference": "in",
                "materialnoof": True,
            }
        )
        self.Cabin(self.positions[(2, 0)], self.positions[(1, 0)])


class BabyFood(Object):
    """Mushy baby food, which the man in the house wants in exchange for a password."""

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {
                "movable": True,
                "object": "baby food",
                "other": "mushy",
                "secondname": "food",
                "pluralreference": "some",
            }
        )


class StartingForest(InsideWorld):
    """The forest north of the starting area: a stream, a spade, trees, a note, and a hut."""

    class Trees(Object):
        """The thick trees."""

        def __init__(self, position):
            super().__init__(position)
            self.properties.update(
                {
                    "movable": False,
                    "object": "trees",
                    "other": "thick, tall",
                    "plural": True,
                    "pluralreference": "some",
                    "reference": "they",
                }
            )

    class Stream(Object):
        """A narrow stream."""

        def __init__(self, position):
            super().__init__(position)
            self.properties.update(
                {"movable": False, "object": "stream", "other": "narrow"}
            )

    class Hut(InsideWorld):
        """A small hut with a table holding baby food."""

        def __init__(self, position, exitpos):
            super().__init__(position, (1, 1), exitpos, (0, 0))
            for pos in self.positions:
                self.inside(pos, "hut", "inside")
            self.properties.update(
                {
                    "movable": False,
                    "object": "hut",
                    "other": "small",
                    "insidereference": "in",
                }
            )
            BabyFood(Table(self.positions[(0, 0)]))

    class Branch(Note):
        """A fallen branch with a code showing the fifth password
        in the first letter of every alternate word (number words
        are actually numbers)"""

        def __init__(self, position):
            super().__init__(
                position,
                "The text is scratched on the fallen branch:\n" + LAST_PASSWD_CODE,
            )
            self.properties.update(
                {
                    "movable": False,
                    "object": "fallen branch",
                    "color": "brown",
                    "other": "medium-sized",
                    "secondname": "branch",
                    "message": "There are some faint scratches on the branch.",
                }
            )

    def __init__(self, position, exitpos):
        super().__init__(position, (5, 5), exitpos, (2, 3), "visible", {(2, 4)})
        self.pointall((2, 4), "exit", "to the forest", False)
        for pos in self.positions:
            self.inside(pos, "forest", "in")
        self.properties.update(
            {
                "movable": False,
                "object": "forest",
                "other": "big, dense",
                "insidereference": "in",
            }
        )
        self.Trees(self.positions[(1, 3)])
        self.Stream(self.positions[(2, 2)])
        Spade(self.positions[(2, 1)])
        self.Branch(self.positions[(2, 0)])
        self.Hut(self.positions[(3, 3)], self.positions[(2, 3)])


class Archway(Object):
    """The large concrete archway leading out of the village."""

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {
                "movable": False,
                "object": "archway",
                "material": "concrete",
                "height": 3,
                "other": "large",
            }
        )


class CodePaper(Note):
    """A piece of paper with a password written as letter and digit codes.
    Each letter becomes let<N> (a=1, b=2, ...) and each digit becomes num<d>, all
    joined with "-".
    """

    def __init__(self, position, passwd):
        """Args:
        position: the position or container that holds the paper.
        passwd: the password to encode on the paper.
        """

        def numify(text):
            """Encode a password as the letter/digit code shown on the paper."""
            ans = []
            for char in text:
                try:
                    int(char)
                except Exception:
                    ans.append(f'let{str(ord(char.lower()) - ord("a") + 1)}')
                else:
                    ans.append(f"num{char}")
            return "-".join(ans)

        num = numify(passwd)
        super().__init__(position, f"The text '{num}' is written on the paper.")
        self.properties.update(
            {
                "movable": True,
                "object": "piece of paper",
                "color": "white",
                "other": "small",
                "secondname": "paper",
            }
        )


class PasswdFolder(ContainerObject):
    """A folder that can be opened and closed and holds the sticky note."""

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {
                "movable": True,
                "object": "folder",
                "color": "white",
                "open": False,
                "objectsare": "inside",
            }
        )

    def open(self, person):
        """Open the folder so its contents can be seen."""
        self.properties["open"] = True
        return "I open the folder."

    def close(self, person):
        """Close the folder so its contents are hidden."""
        self.properties["open"] = False
        return "I close the folder."


class PasswdStickyNote(Note):
    """A sticky note with a password on it."""

    def __init__(self, position, passwd):
        """Args:
        position: the position or container that holds the sticky note.
        passwd: the password written on it.
        """
        super().__init__(position, f"'{passwd}' is written on the sticky note.")
        self.properties.update(
            {
                "movable": True,
                "object": "sticky note",
                "color": "yellow",
                "other": "small square",
            }
        )


class PasswordNote(Note):
    """A dusty old note with a password on it."""

    def __init__(self, position, passwd):
        """Args:
        position: the position or container that holds the note.
        passwd: the password written on it.
        """
        super().__init__(position, f"The note shows the text '{passwd}'.")
        self.properties.update(
            {"movable": True, "object": "note", "other": "dusty old"}
        )


class Table(ContainerObject):
    """A heavy wooden table that things can be put on."""

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {
                "movable": False,
                "object": "table",
                "color": "brown",
                "material": "wood",
                "height": 1,
                "objectsare": "on",
                "message": "The table looks too heavy to move.",
            }
        )


class LockedBox(ContainerObject):
    """A strong iron box that can be locked and unlocked with a key."""

    def __init__(self, position):
        super().__init__(position)
        self.lock()
        self.closedname = "locked box"
        self.openname = "open box"
        self.properties.update(
            {
                "movable": True,
                "color": "metal-colored",
                "material": "iron",
                "objectsare": "in",
                "other": "strong",
                "secondname": "box",
                "key": "key",
            }
        )

    def lock(self):
        """Close and lock the box."""
        self.properties.update({"open": False, "object": "locked box"})

    def unlock(self):
        """Unlock and open the box."""
        self.properties.update({"open": True, "object": "open box"})


class MainDevice(ContainerObject):
    """The electronic device holding the experiment's result.
    Contains the lights showing how many passwords were accepted, and a button that
    asks for a password when pressed.
    """

    class Button:
        """The red button. Pressing it prompts for a password."""

        def __init__(self, device):
            """Args:
            device: the MainDevice the button is on.
            """
            self.position = device
            self.properties = {"type": "button"}
            self.device = device
            self.properties.update(
                {
                    "movable": False,
                    "object": "button",
                    "color": "red",
                    "message": "I don't think you should press it.",
                    "usable": True,
                }
            )
            self.entered = set()
            self.position.add(self)
            self.use = self.press

        def move(self, position):
            """Move this button to another container."""
            self.position.remove(self)
            self.position = position
            self.position.add(self)

        def delete(self):
            """Remove this button from the game."""
            self.position.remove(self)

        def press(self, person):
            """Prompt for a password and react to it.
            A wrong or repeated password destroys the device (loss). Entering all of the
            passwords opens it (win). Otherwise another light is turned on.
            Returns:
                An EndGame for a win or loss, or a message.
            """
            code = getinput("code: ").strip().lower()
            printoutput()
            if code in self.entered or code not in PASSWORDS:
                return EndGame(
                    "The device explodes!\nYou now have no way of recovering your work.",
                    "You should have been more careful.",
                    False,
                )
            self.entered.add(code)
            if self.entered == set(PASSWORDS):
                return EndGame(
                    "Another light turns on in the device.\nAll the lights are now on!\nThe device opens to show the result of your experiment... \x1b[0m\x1b[1;40;36m 42 ",
                    "You have recovered your hard work!",
                    True,
                )
            self.device.lights.turnon(1)
            return (
                "Another light turns on in the device!"
                if len(self.entered) != 1
                else "A light turns on in the device!"
            )

    class Lights(Object):
        """The green lights showing how many passwords have been accepted."""

        def __init__(self, device):
            """Args:
            device: the MainDevice the lights are on.
            """
            super().__init__(device)
            self.total = len(PASSWORDS)
            self.on = 0
            self.properties.update(
                {
                    "movable": False,
                    "object": "lights",
                    "plural": True,
                    "color": "green",
                    "message": f"None of the lights are on out of {self.total} total lights.",
                    "reference": "they",
                    "pluralreference": "some",
                    "secondname": "light",
                }
            )

        def turnon(self, n):
            """Turn on n more lights and update the description."""
            self.on += n
            self.properties["message"] = (
                f'{self.on} {"\x1b[1m\x1b[38;5;136mlights\x1b[0m are" if self.on != 1 else "\x1b[1m\x1b[38;5;136mlight\x1b[0m is"} on out of {self.total} total \x1b[1m\x1b[38;5;136mlights\x1b[0m.'
            )

    def __init__(self, position):
        """Args:
        position: the position that holds the device.
        """
        super().__init__(position)
        self.lights = self.Lights(self)
        self.add(button := self.Button(self))
        self.use = button.press
        self.properties.update(
            {
                "movable": True,
                "object": "electronic device",
                "objectsare": "on",
                "message": "The device looks very important.",
                "secondname": "device",
                "putinside": False,
                "usable": True,
            }
        )


class Spade(Object):
    """A sharp spade used to dig up the patch of sand."""

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {"movable": True, "object": "spade", "other": "sharp", "usable": True}
        )

    def use(self, person, object_=None):
        """Use the spade on something diggable.
        Args:
            person: the Person using it.
            object_: the target, or None if the player did not name one.
        """
        if not object_:
            return "What should I use it on?"
        if "digable" in object_.properties and object_.properties["digable"] == True:
            return object_.dig(person, self)
        else:
            return "That's ridiculous."


class SandPatch(ContainerObject):
    """A patch of sand hiding an object; it is uncovered after three digs."""

    def __init__(self, position, object_, uncvrmsg):
        """Args:
        position: the position that holds the patch.
        object_: class of the object hidden underneath, called with the patch as its position.
        uncvrmsg: message shown when the object is uncovered.
        """
        super().__init__(position)
        self.object_ = object_
        self.uncvrmsg = uncvrmsg
        self.properties.update(
            {
                "movable": False,
                "object": "patch of sand",
                "color": "white",
                "other": "small",
                "digable": True,
                "objectsare": "on",
                "putinside": False,
                "secondname": "sand",
                "digtool": "spade",
                "nodigtoolmessage": "I need a tool to do that...",
            }
        )
        self.diglevel = 0

    def dig(self, person, tool):
        """Dig once. The hidden object appears on the third dig.
        Returns:
            A message describing the progress.
        """
        self.diglevel += 1
        if self.diglevel == 3:
            return (self.uncvrmsg, self.object_(self))[0]
        elif self.diglevel == 1:
            return "I make some progress."
        elif self.diglevel == 2:
            return "I make some more progress."
        else:
            return "I can't dig any more."


class InvisibilityCloak(Object):
    """A cloak that makes the player invisible to NPCs while worn."""

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {
                "movable": True,
                "object": "cloak",
                "color": "silver",
                "material": "a very soft material",
                "other": "shimmering",
                "usable": True,
                "wearable": True,
            }
        )
        self.use = self.wear

    def wear(self, person):
        """Put the cloak on and become invisible."""
        person.wear(self)
        person.properties["invisible"] = True
        return "I wear the cloak."

    def unwear(self, person):
        """Take the cloak off and become visible again."""
        person.unwear(self)
        person.properties["invisible"] = False
        return "I remove the cloak."


class CloakChest(ContainerObject):
    """A small golden chest holding the invisibility cloak."""

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {
                "movable": True,
                "object": "chest",
                "color": "golden",
                "material": "a light metal",
                "other": "small",
                "objectsare": "in",
                "open": True,
            }
        )
        InvisibilityCloak(self)


class Key(Object):
    """An old iron key that locks and unlocks the box it was made for."""

    def __init__(self, position, object_):
        """Args:
        position: the position or container that holds the key.
        object_: the box it fits; its names become the things the key can be used on.
        """
        super().__init__(position)
        self.properties.update(
            {
                "movable": True,
                "object": "key",
                "color": "faded black",
                "material": "iron",
                "other": "old, heavy,",
                "usable": True,
            }
        )
        self.properties["objectsusableon"] = {
            object_.openname,
            object_.closedname,
            object_.properties["secondname"],
        }

    def use(self, person, object_=None):
        """Toggle the lock of the box.
        Args:
            person: the Person using the key.
            object_: the box to unlock or lock, or None if the player did not name one.
        """
        if not object_:
            return "What should I use it on?"
        if not object_.properties["open"]:
            object_.unlock()
            return "The box\x1b[0m opens with a click."
        else:
            object_.lock()
            return "I close and lock the box\x1b[0m."


class Apple(Object):
    """A shiny red apple."""

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {"movable": True, "object": "apple", "color": "red", "other": "shiny"}
        )


class Banana(Object):
    """A soft yellow banana."""

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {
                "movable": True,
                "object": "banana",
                "color": "yellow",
                "other": "soft, tasty-looking",
            }
        )


class Coin(Object):
    """A shiny solid gold coin."""

    def __init__(self, position):
        super().__init__(position)
        self.properties.update(
            {
                "movable": True,
                "object": "coin",
                "color": "golden",
                "material": "solid gold",
                "other": "shiny",
            }
        )


class Person:
    """The player character."""

    class Inventory:
        """The set of objects the player is carrying; acts as a container for them."""

        def __init__(self):
            self.holding = set()

        def add(self, object_):
            """Put an object in the inventory."""
            self.holding.add(object_)

        def remove(self, object_):
            """Take an object out of the inventory."""
            self.holding.remove(object_)

    def __init__(self, position):
        """Args:
        position: the WorldPosition the player starts on.
        """
        self.position = position
        self.inventory = self.Inventory()
        self.properties = {"type": "person", "object": "person", "movable": True}
        self.position.add(self)
        self.wearing = set()

    def wear(self, object_):
        """Mark an object as worn."""
        self.wearing.add(object_)

    def unwear(self, object_):
        """Mark an object as no longer worn."""
        self.wearing.remove(object_)

    def move(self, place):
        """Move the player to another position."""
        self.position.remove(self)
        self.position = place
        self.position.add(self)

    def take(self, object_):
        """Pick an object up into the inventory if it is movable."""
        if object_.properties["movable"]:
            object_.move(self.inventory)

    def drop(self, object_, newcontainer=None):
        """Move an object from the inventory to a container or the current position.
        Args:
            object_: the object to drop.
            newcontainer: where to put it; defaults to the player's position.
        """
        if newcontainer is None:
            newcontainer = self.position
        if object_ in self.inventory.holding:
            object_.move(newcontainer)


class Game:
    """The game engine: builds the island, parses commands and runs the main loop.
    Command handlers (take, drop, examine, ...) each receive the rest of the typed
    line as a single string and return the text to show, or an EndGame. Handlers
    that change the world usually return (message, self.updateobjectindex())[0] so
    the index of visible objects is refreshed while the message is still returned.
    Dialogue trees returned by NPCs are lists of [text, next] where next is:
        None    the conversation ends after text
        dict    numbered options: {choice text: another [text, next]}
        tuple   objects (or a call that creates them) handed to the player
    """

    def __init__(self):
        """Register all command words and their aliases, then build the game world."""

        def look(inputstring=None):
            """Handle 'look' (look around) and 'look <thing>' (examine, text only)."""
            if not inputstring:
                return self.lookaround()
            else:
                resolved = self.resolve(inputstring)
                if type(resolved) == str:
                    return resolved
                return "Where did you learn English?"

        # Number of counted commands the player gets before losing.
        self.maxcommands = 200
        # Command word(s) -> handler. Several words can map to the same handler.
        self.commands = {
            "help": self.helpcommands,
            "walk": self.move,
            "move": self.move,
            "go": self.move,
            "take": self.take,
            "pick up": self.take,
            "get": self.take,
            "drop": self.drop,
            "leave": self.drop,
            "i": self.listinv,
            "inventory": self.listinv,
            "look": look,
            "look around": self.lookaround,
            "ex": self.examine,
            "exam": self.examine,
            "examine": self.examine,
            "look at": self.examine,
            "inspect": self.examine,
            "put": self.put,
            "talk": self.talk,
            "talk to": self.talk,
            "use": self.use,
            "give": self.give,
            "read": self.read,
            "press": self.press,
            "push": self.press,
            "dig": self.dig,
            "wear": self.wear,
            "put on": self.wear,
            "remove": self.unwear,
            "take off": self.unwear,
            "exit": self.exit,
            "walk out": self.exit,
            "go out": self.exit,
            "move out": self.exit,
            "out": self.exit,
            "open": self.open,
            "close": self.close,
            "ride": self.ride,
            "mount": self.ride,
            "enter": self.enter,
            "moves": lambda: f"\x1b[32mYou have {self.maxcommands - self.donecommands} commands left.\x1b[0m",
            "commands": lambda: f"\x1b[32mYou have {self.maxcommands - self.donecommands} commands left.\x1b[0m",
            "save": self.save,
            "load": self.load,
            "quit": self.quit,
        } | {cmd + " exit": self.exit for cmd in ("walk", "go", "move", "enter")}

        def make_move(dir_):
            """Create a no-argument command that moves in a fixed direction."""

            def move():
                return self.move(dir_)

            return move

        # Bare direction words (and their first letters) act as move commands.
        for dir_ in ("north", "south", "east", "west"):
            for dir__ in (dir_, dir_[0]):
                self.commands[dir__] = make_move(dir__)
        # Words that start a multi-word command (like 'pick' in 'pick up'), used by parse.
        self.ongoingcommands = set()
        for command in self.commands:
            words = command.split()
            if len(words) > 1:
                for word in words[:-1]:
                    self.ongoingcommands.add(word)

    def prompt(self, prompt, default=False):
        """Yes or no prompts. Repeat 3 times for invalid
        input, then return default value."""
        ans = getinput(prompt).strip().lower()
        if not ans:
            return default
        ans = ans[0]
        if ans == "y":
            return True
        elif ans == "n":
            return False
        for i in range(2):
            ans = getinput(
                f"\x1b[1m\x1b[31m[invalid input ({i + 2}/3)\x1b[0m " + prompt
            )
            if ans == "y":
                return True
            elif ans == "n":
                return False
        return default

    def quit(self):
        """Quit game after asking to save game if progress
        is made."""

        if [x for x in inputs if x.strip().lower() not in ("save", "load", "quit")] and self.prompt(
            "\x1b[1m\x1b[33msave game before quitting? (y/N): \x1b[0m"
        ):
            if saveoutput := self.save():
                printoutput(saveoutput)
        return EndGame(None, "bye", None)

    def getfile(self, filefor):
        """Prompt for a file path. If filefor is 'save',
        warn for overwriting file. If filefor is 'load',
        error on nonexistent file. If left blank, return
        None."""
        import os.path

        prompt = (
            "\x1b[1m" + ("Save" if filefor == "save" else "Load") + " File: \x1b[0m"
        )
        fn = getinput(prompt).strip()
        if not fn:
            return
        fn = os.path.abspath(os.path.expanduser(fn))
        if filefor == "save" and os.path.exists(fn):
            if os.path.isdir(fn):
                printoutput("\x1b[31merror: already existing directory\x1b[0m")
                return
            if not self.prompt(
                "\x1b[1m\x1b[33mfile already exists. overwrite? (y/N): \x1b[0m"
            ):
                return self.getfile(filefor)
            else:
                return fn
        elif filefor == "load" and not os.path.exists(fn):
            printoutput("\x1b[31merror: file does not exist\x1b[0m")
        elif filefor == "load" and os.path.isdir(fn):
            printoutput("\x1b[31merror: is a directory\x1b[0m")
        else:
            return fn

    def save(self):
        """Save game state into a file. Write all inputs including
        commands to load."""
        fn = self.getfile("save")
        if fn is None:
            return
        try:
            with open(fn, "w", encoding="utf-8") as file:
                file.write("\n".join(inputs))
        except Exception as e:
            return f"\x1b[31merror: {e}\x1b[0m"
        else:
            return f"\x1b[32m\x1b[3msaved game\x1b[0m"

    def load(self):
        global queued_inputs
        """Load previously saved game. Quit the game if error."""
        if [x for x in inputs if x.strip().lower() not in ("save", "load", "quit")] and not self.prompt(
            "\x1b[1m\x1b[33mdiscard current game? (y/N): \x1b[0m"
        ):
            return

        fn = self.getfile("load")
        if fn is None:
            return
        try:
            with open(fn, "r", encoding="utf-8") as file:
                self.reset()
                queued_inputs = file.read().split("\n")
                # Normal loop() till queued_inputs is empty.
                while queued_inputs:
                    # One typed command per iteration; an EndGame result finishes the round.
                    command = getinput("\n> ")
                    printoutput()
                    output = self.parse(command)
                    if type(output) == EndGame:
                        if not output.description and not output.win:
                            printoutput(output.endmessage)
                            return
                        printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
                        printoutput(
                            "\x1b[3m"
                            + output.description
                            + "\x1b[0m\n\n"
                            + output.endmessage
                            + "\n\n"
                            + (
                                "\x1b[1m\x1b[32mYOU WIN!\x1b[0m"
                                if output.win
                                else "\x1b[1m\x1b[31mYOU LOSE.\x1b[0m"
                            )
                            + "\n"
                        )
                        while True:
                            playagain = (
                                getinput(
                                    "\x1b[1m\x1b[32mDo you want to play again? (yes/no): \x1b[0m"
                                )
                                .strip()
                                .lower()
                            )
                            if playagain in ("yes", "no"):
                                break
                            else:
                                printoutput("That is not a valid option.\n")
                        if playagain == "yes":
                            break
                        else:
                            printoutput("\nbye")
                            return
                    if output:
                        printoutput(output)
                        if 0 < self.maxcommands - self.donecommands - 1 <= 20:
                            printoutput(
                                f'\n\n\x1b[31mYou only have {self.maxcommands - self.donecommands - 1} {"commands" if self.maxcommands - self.donecommands - 1 != 1 else "command"} left!\x1b[0m'
                            )
                        elif self.maxcommands - self.donecommands - 1 == 0:
                            printoutput(f"\n\n\x1b[31mYou have 0 commands left!\x1b[0m")
                        if command.strip().lower() not in (
                            "moves",
                            "commands",
                        ) and output not in (
                            "\x1b[31mSorry, I don't understand.\x1b[0m",
                            "Time passes...",
                        ):
                            self.donecommands += 1
                            if self.donecommands == self.maxcommands:
                                printoutput(
                                    "\n\x1b[31mOh no! It is too late. Your rivals have come back to the island and destroyed the device! You have now lost your hard work forever.\x1b[0m\n"
                                )
                                while True:
                                    playagain = (
                                        getinput(
                                            "\x1b[1m\x1b[32mDo you want to play again? (yes/no): \x1b[0m"
                                        )
                                        .strip()
                                        .lower()
                                    )
                                    if playagain in ("yes", "no"):
                                        break
                                    else:
                                        printoutput("That is not a valid option.\n")
                                if playagain == "yes":
                                    break
                                else:
                                    printoutput("\nbye")
                                    return
        except Exception as e:
            exitwith(f"\x1b[31merror: {e}\x1b[0m")
        else:
            return

    def helpcommands(self):
        """Show the help pages on the alternate screen."""
        printoutput("\x1b[?1049h", end="")
        printoutput(f"""\
When at the > prompt, type actions in the format:

    \x1b[1mcommand\x1b[0m \x1b[3minput\x1b[0m

Examples: '\x1b[1mtake\x1b[0m \x1b[3mapple from table\x1b[0m', '\x1b[1mgive\x1b[0m \x1b[3mapple to man\x1b[0m', '\x1b[1mexam\x1b[0m \x1b[3mditch\x1b[0m'
If the game returns 'Sorry, I don't understand.' for your command, try using another word with the same meaning.
You have {self.maxcommands} total commands to finish the game before you lose. Empty inputs and commands which return 'Sorry, I don't understand.' will not be counted.
When talking to an NPC, you will be given numbered options like this after the NPC dialogue:

    Hello.

    \x1b[3m[1] Hi\x1b[0m
    \x1b[3m[2] Bye\x1b[0m

    \x1b[3moptions: 1/2\x1b[0m.

Then you will have to enter only a number from the given options. You cannot type normal game commands here or exit this prompt yourself.
""")
        getinput("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m")
        printoutput("\x1b[H\x1b[2J", end="")
        printoutput(f"""\
Always try to examine all objects you see.
All passwords are of a similar type. (Example: 123, 456, ... or abc, def, ...)
No passwords look very different from the others (for example gh6f2z3 and MARLIN).
Don't go any place where you can't see anything around.
If an object inside a container is not listed in look around (if it is inside a container inside another container), you can access it with \x1b[1mcommand\x1b[0m \x1b[3mobject in container\x1b[0m. For example, \x1b[1mtake\x1b[0m \x1b[3mapple\x1b[0m will not work when the apple is inside a box which is on a table, but \x1b[1mtake\x1b[0m \x1b[3mapple from box\x1b[0m will.
Moving in any direction always also looks around, you don't need to retype look.

\x1b[1m\x1b[4mUseful commands:\x1b[0m

- \x1b[1mnorth\x1b[0m / \x1b[1msouth\x1b[0m / \x1b[1meast\x1b[0m / \x1b[1mwest\x1b[0m / \x1b[1mout\x1b[0m
(or \x1b[1mwalk\x1b[0m / \x1b[1mgo\x1b[0m / \x1b[1mmove\x1b[0m \x1b[3mnorth\x1b[0m / \x1b[3msouth\x1b[0m / \x1b[3meast\x1b[0m / \x1b[3mwest\x1b[0m / \x1b[3mout\x1b[0m)
- \x1b[1mtake\x1b[0m / \x1b[1mpick up\x1b[0m / \x1b[1mget\x1b[0m \x1b[3mobject\x1b[0m
- \x1b[1mex\x1b[0m / \x1b[1mexam\x1b[0m / \x1b[1mexamine\x1b[0m / \x1b[1mlook at\x1b[0m / \x1b[1minspect\x1b[0m \x1b[3mobject\x1b[0m
- \x1b[1mtalk\x1b[0m / \x1b[1mtalk to\x1b[0m \x1b[3mperson\x1b[0m
- \x1b[1muse\x1b[0m \x1b[3mobject\x1b[0m
- \x1b[1muse\x1b[0m \x1b[3mobject_1 on object_2\x1b[0m
""")
        getinput("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m")
        printoutput("\x1b[?1049l", end="")
        return "\x1b[3mHelp done.\x1b[0m"

    def reset(self):
        global inputs, queued_inputs
        """Reset (or initialize) the world."""
        # Build the island. The player starts at (5, 5) next to a table and the locksmith.
        self.world = World((10, 10))
        Apple(table := Table(self.world.positions[(5, 5)]))
        PasswordNote(LockedBox(table), PASSWORDS[0])
        StartingMan(self.world.positions[(4, 5)])
        StartingPath(self.world, (6, 5), (8, 5))
        for x in range(10):
            StartingDitch(self.world.positions[(x, 6)], 2)
        for x in range(6):
            for y in range(7, 10):
                self.world.inside((x, y), "village", "in")
        StartingHorse(
            self.world.positions[(7, 5)],
            self.world.positions[(7, 7)],
            "I jump over the \x1b[1m\x1b[38;5;136mditch\x1b[0m on the horse.",
        )
        StartingForest(self.world.positions[(7, 4)], self.world.positions[(7, 5)])
        MainDevice(self.world.positions[(7, 7)])
        Coin(ContainerWall(self.world.positions[(9, 5)], 4))
        Watchman(Archway(self.world.positions[(6, 7)]), self.world.positions[(7, 7)])
        self.world.positions[(6, 7)].point("village", None, False, "west")
        self.world.positions[(5, 7)].point(
            "archway", "leading out of the village", False, "east", "archway"
        )
        for y in range(8, 10):
            NormalWall(self.world.positions[(6, y)], 3)
        JokeMan(
            House(self.world.positions[(5, 8)], self.world.positions[(5, 7)]).positions[
                (1, 0)
            ],
            (
                "Hi, my name is Transylvanian Cross-Country Discombobulating Green Apple Cooker Rajuson B. B. Jeff Herfet. (try talking to me again)",
                "Look! I'm inside a house! Hey, have you ever seen a house before? (try talking to me again)",
                "Woooo! I'm flying! Yay! (try talking to me again)",
                "My name is Jeff! (try talking to me again)",
                "A, b, c, d, e, f, g ... w, x, y, and z! Now I know my ABC, I can finally learn my numbers. Hey, do you know numbers? I've heard they're really hard to learn. (try talking to me again)",
                "Look! It's a polar bear! Ha, I tricked you! (try talking to me again)",
                "I love eating baby food, but dog biscuits are also not bad. (try talking to me again)",
                "Hello. This is my house, I live here. You're welcome back at any time! (try talking to me again)",
            ),
            self.world.positions[(4, 7)],
        )
        SandPatch(
            self.world.positions[(3, 5)],
            CloakChest,
            "I uncover a \x1b[1m\x1b[38;5;136mchest\x1b[0m!",
        )
        Ship(self.world.positions[(9, 7)], self.world.positions[(8, 7)])
        self.person = Person(self.world.positions[(5, 5)])
        self.updateobjectindex()
        self.donecommands = 0
        # List of all the inputs by the player, including commands. Used for
        # save and load game.
        inputs = []
        # Queued inputs. If not empty, getinput pops the first one. Used for
        # load game.
        queued_inputs = []

    def setup(self):
        """Create a fresh island, put the player on it and show the introduction.
        Called at the start and again every time the player chooses to play again."""
        self.reset()
        printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
        printoutput("""\
\x1b[1m\x1b[32m\
╔═════════════════════════╗
║ QUEST FOR THE FIVE KEYS ║
║\x1b[39m\x1b[3m\
 A text adventure game\
\x1b[32m\x1b[23m\
   ║
╚═════════════════════════╝\
\x1b[0m
""")
        getinput("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m")
        printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
        printoutput("""\
You are one of the world\'s foremost research scientists. After years of work, you had finally completed the greatest experiment of your career.

Before you could present your discovery, your rivals stole the results of your experiment and fled to a remote island. There they took extraordinary measures to ensure no one could recover your work.

The complete result is sealed inside a high-security electronic device of your own making that you were using to store your work. It can only be opened by entering \x1b[1m\x1b[32mfive different passwords\x1b[0m, each hidden somewhere on the island. Beware! Enter a single incorrect password and the device will destroy itself, taking your experiment with it forever.

The island is inhabited. Its people know nothing of your rivals' actions, but some may help you if you can persuade them, while others may have something you need.

Can you recover the five passwords, unlock the device, and reclaim your stolen work? Your success depends entirely on your ingenuity.

Your fate-and the fate of your experiment-is now in your hands.\
""")
        getinput("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m")
        printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
        printoutput("""\
To help you in your quest, here are some tips:

Always examine objects, however unrelated you think they are.

Always follow paths to their end.

Don't go anywhere you can't see anything around, you will just waste commands.

When talking to an NPC, you can only select one of the numbered options given by the game by typing the exact number you want. For example, you might be given a prompt like options:1/2/3. Then, you will be able to select 1, 2, or 3.

If a command does not work, try using another word with the same meaning.

Moving in any direction always also looks around. You don't need to retype look after going somewhere.

You can always interact with an object if it is in view (listed in 'look').\
""")
        getinput("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m")
        printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
        printoutput(f"""\
When interacting with an object in any way (pick up, examine, talk to, etc), you don't need to type the full name. You can usually use only one word. For example, take box instead of take locked box, exam device instead of exam electronic device, etc.

If an object inside a container is not listed in look around (if it is inside a container inside another container), you can access it with \x1b[1mcommand\x1b[0m \x1b[3mobject in container\x1b[0m. For example, take apple will not work when the apple is inside a box which is on a table, but take apple from box will.

You can also use abbreviations for commands, like exam instead of examine, talk instead of talk to, etc. They are also given in the help.

You are allowed to use a maximum of {self.maxcommands} commands, including 'help'. After that, it will be too late to recover your work and you will have lost the game.

Type 'moves' or 'commands' at any time to see the number of commands you have left.

Type 'save' to export a file from which you can later continue play. Load a saved file with 'load'.

Quitting is for losers, but you can do it by typing 'quit'.

Good luck!\
""")
        getinput("\x1b[1m\x1b[31m[Press Enter to continue]\x1b[0m")
        printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
        self.reset()
        printoutput(self.lookaround())

    def anifier(self, word, pospointsindir=None):
        """Return a word with its article and highlight color, like 'an apple' or 'some trees'.
        Args:
            word: the object name.
            pospointsindir: the highlighted things pointed at from the current cell,
                used to attach their following phrase ("a forest to the north").
        """
        if pospointsindir:
            if word in pospointsindir and pospointsindir[word][1]:
                return (
                    pospointsindir[word][1]
                    + " \x1b[1m\x1b[38;5;136m"
                    + word
                    + (
                        "\x1b[0m " + pospointsindir[word][0]
                        if pospointsindir[word][0]
                        else "\x1b[0m"
                    )
                )
            elif word in pospointsindir:
                if word[0].lower() in ("a", "e", "i", "o", "u"):
                    return (
                        "an \x1b[1m\x1b[38;5;136m"
                        + word
                        + (
                            "\x1b[0m " + pospointsindir[word][0]
                            if pospointsindir[word][0]
                            else "\x1b[0m"
                        )
                    )
                else:
                    return (
                        "a \x1b[1m\x1b[38;5;136m"
                        + word
                        + (
                            "\x1b[0m " + pospointsindir[word][0]
                            if pospointsindir[word][0]
                            else "\x1b[0m"
                        )
                    )
        if word in self.objectindex:
            obj = self.objectindex[word]
            if "pluralreference" in obj.properties:
                return (
                    obj.properties["pluralreference"]
                    + " \x1b[1m\x1b[38;5;136m"
                    + word
                    + "\x1b[0m"
                )
        if word[0].lower() in ("a", "e", "i", "o", "u"):
            return "an \x1b[1m\x1b[38;5;136m" + word + "\x1b[0m"
        else:
            return "a \x1b[1m\x1b[38;5;136m" + word + "\x1b[0m"

    def formatplural(self, listobj, pospointsindir=None):
        """Join descriptions into a sentence list: 'a x', 'a x and a y' or 'a x, a y, and a z'."""
        anified = [self.anifier(obj, pospointsindir) for obj in listobj]
        if len(anified) == 1:
            return anified[0]
        returnstring = ""
        if len(anified) == 2:
            returnstring += " and ".join(anified)
        else:
            returnstring += ", ".join(anified[:-1])
            returnstring += ", and " + anified[-1]
        return returnstring

    def close(self, inputstring=None):
        """Close a container, using its key when it needs one."""
        if not inputstring:
            return "What should I close?"
        resolved = self.resolve(inputstring)
        if type(resolved) == str:
            return resolved
        else:
            object_, objectname, holder, holdername = resolved
        if "open" not in object_.properties:
            return "That's ridiculous."
        if object_.properties["open"] == False:
            if "plural" in object_.properties and object_.properties["plural"] == True:
                return "They're already closed."
            else:
                return "It's already closed."
        if "key" in object_.properties:
            if object_.properties["key"] not in (
                inv := {
                    obj.properties["object"]: obj
                    for obj in self.person.inventory.holding
                }
            ):
                if (
                    "plural" in object_.properties
                    and object_.properties["plural"] == True
                ):
                    reference = "them"
                else:
                    reference = "it"
                return f'I need {self.anifier(object_.properties["key"])} to close {reference}...'
            return (
                inv[object_.properties["key"]].use(self.person, object_),
                self.updateobjectindex(),
            )[0]
        else:
            return (object_.close(self.person), self.updateobjectindex())[0]

    def open(self, inputstring=None):
        """Open a container, using its key when it needs one."""
        if not inputstring:
            return "What should I open?"
        resolved = self.resolve(inputstring)
        if type(resolved) == str:
            return resolved
        else:
            object_, objectname, holder, holdername = resolved
        if "open" not in object_.properties:
            return "That's ridiculous."
        if object_.properties["open"] == True:
            if "plural" in object_.properties and object_.properties["plural"] == True:
                return "They're already open."
            else:
                return "It's already open."
        if "key" in object_.properties:
            if object_.properties["key"] not in (
                inv := {
                    obj.properties["object"]: obj
                    for obj in self.person.inventory.holding
                }
            ):
                if (
                    "plural" in object_.properties
                    and object_.properties["plural"] == True
                ):
                    reference = "them"
                else:
                    reference = "it"
                return f'I need {self.anifier(object_.properties["key"])} to open {reference}...'
            return (
                inv[object_.properties["key"]].use(self.person, object_),
                self.updateobjectindex(),
            )[0]
        else:
            return (object_.open(self.person), self.updateobjectindex())[0]

    def listinv(self):
        """Describe what the player is carrying."""
        if self.inventoryobjects:
            return (
                "I am carrying "
                + self.formatplural(sorted(list(self.inventoryobjects)))
                + "."
            )
        else:
            return "I am not carrying anything."

    def exit(self):
        """Leave the current inside world for its parent world."""
        if self.world.properties["type"] == "world":
            return "I'm already outside."
        if (
            self.world.exitablepositions
            and self.person.position.worldposition not in self.world.exitablepositions
        ):
            return "I can't go out from here."
        outside = self.world.exitposition
        self.world = outside.world
        self.person.move(outside)
        self.updateobjectindex()
        return "Going \x1b[1m\x1b[38;5;30mout\x1b[0m...\n\n" + self.lookaround()

    def lookaround(self):
        """Describe the surroundings: where the player is, the paths nearby and the objects around.
        Also clears the screen, so the result replaces whatever was shown before.
        """
        returnstring = ""
        objectsfound = False
        if "inside" in self.person.position.properties:
            returnstring += (
                "I am "
                + self.person.position.properties["insidereference"]
                + " "
                + (
                    self.anifier(self.person.position.properties["inside"])
                    if "pluralreference" not in self.person.position.properties
                    else self.person.position.properties["pluralreference"]
                    + " \x1b[1m\x1b[38;5;136m"
                    + self.person.position.properties["inside"]
                    + "\x1b[0m"
                )
                + ".\n\n"
            )
        # Offset from the player -> label used in the description.
        dirnames = {
            (0, 0): "Right next to me",
            (0, 1): "On my north",
            (0, -1): "On my south",
            (1, 0): "On my east",
            (-1, 0): "On my west",
            (1, 1): "On my north-east",
            (-1, 1): "On my north-west",
            (1, -1): "On my south-east",
            (-1, -1): "On my south-west",
        }
        if self.aroundpaths:
            dirpaths = {
                key: []
                for key in (
                    ("north", "south"),
                    ("south", "north"),
                    ("north-east", "north-west"),
                    ("north-west", "north-east"),
                    ("south-east", "south-west"),
                    ("south-west", "south-east"),
                    ("east", "west"),
                    ("west", "east"),
                )
            }
            for pathname in self.aroundpaths:
                path, a, b, dir_, pos = self.aroundpaths[pathname]
                if pos == (myposition := self.person.position.worldposition):
                    objectsfound = True
                    pathstring = (
                        f'I am {path.properties["insidereference"]} {self.anifier(pathname)}. '
                        + "It continues to my "
                    )
                else:
                    diffx, diffy = (
                        abs(pos[0] - myposition[0]) / (pos[0] - myposition[0])
                        if pos[0] != myposition[0]
                        else 0
                    ), (
                        abs(pos[1] - myposition[1]) / (pos[1] - myposition[1])
                        if pos[1] != myposition[1]
                        else 0
                    )
                    key = dirnames[(diffx, diffy)]
                    objectsfound = True
                    pathstring = (
                        "There is "
                        + self.anifier(pathname)
                        + " "
                        + (
                            " ".join(key.split(" ")[:2])
                            + " \x1b[1m\x1b[38;5;30m"
                            + key.split(" ")[2]
                            + "\x1b[0m"
                            if key.startswith("On my")
                            else "Right \x1b[1m\x1b[38;5;30mnext to me\x1b[0m"
                        ).lower()
                        + ". It leads "
                    )
                dirextends = []
                if pos != a:
                    dirextends.append("\x1b[1m\x1b[38;5;30m" + dir_[0] + "\x1b[0m")
                if pos != b:
                    dirextends.append("\x1b[1m\x1b[38;5;30m" + dir_[1] + "\x1b[0m")
                pathstring += " and ".join(dirextends) + "."
                dirpaths[dir_].append(pathstring)
            for pathstrings in dirpaths.values():
                for pathstring in pathstrings:
                    returnstring += pathstring + "\n"
            returnstring += "\n"
        # Group the visible objects by direction.
        dirobjects = {dn: set() for dn in dirnames.values()}
        holding = {}
        for objectname in self.aroundobjects:
            if objectname in self.person.position.properties["skip"]:
                continue
            obj, dir_ = self.aroundobjects[objectname]
            if type(dir_) == list:
                continue
            if obj.properties["type"] in ("container", "blocking-container"):
                if (
                    "hiddeninside" in obj.properties
                    and obj.properties["hiddeninside"] == True
                ):
                    pass
                else:
                    holding[objectname] = obj.holding
            dirname = dirnames[dir_]
            dirobjects[dirname].add(objectname)
        pospoints = self.person.position.properties["points"]
        dirobjects = {key: sorted(list(dirobjects[key])) for key in dirobjects}
        for key in dirobjects:
            objectsincurdir = dirobjects[key]
            if not pospoints[key] and not objectsincurdir:
                continue
            objectsfound = True
            returnstring += (
                " ".join(key.split(" ")[:2])
                + " \x1b[1m\x1b[38;5;30m"
                + key.split(" ")[2]
                + "\x1b[0m"
                if key.startswith("On my")
                else "Right \x1b[1m\x1b[38;5;30mnext to me\x1b[0m"
            )
            if not pospoints[key]:
                if len(objectsincurdir) == 1:
                    if (
                        "plural" in self.objectindex[objectsincurdir[0]].properties
                        and self.objectindex[objectsincurdir[0]].properties["plural"]
                        == True
                    ):
                        plural = True
                    else:
                        plural = False
                else:
                    plural = True
            else:
                if objectsincurdir:
                    plural = True
                else:
                    if len(pospoints[key]) > 1:
                        plural = True
                    else:
                        plural = next(iter(pospoints[key].values()))[1]
            returnstring += " are " if plural else " is "
            returnstring += (
                self.formatplural(
                    sorted(objectsincurdir + list(pospoints[key])), pospoints[key]
                )
                + ".\n"
            )
            for obj in objectsincurdir:
                if obj in holding and holding[obj]:
                    returnstring += (
                        self.objectindex[obj].properties["objectsare"].capitalize()
                        + f" the \x1b[1m\x1b[38;5;136m{obj}\x1b[0m "
                    )
                    if len(holding[obj]) == 1:
                        if (
                            "plural" in next(iter(holding[obj])).properties
                            and next(iter(holding[obj])).properties["plural"] == True
                        ):
                            plural = True
                        else:
                            plural = False
                    else:
                        plural = True
                    returnstring += "are " if plural else "is "
                    returnstring += (
                        self.formatplural(
                            sorted([hobj.properties["object"] for hobj in holding[obj]])
                        )
                        + ".\n"
                    )
            returnstring += "\n"
        if not objectsfound:
            returnstring = "I don't see anything around here."
        printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
        return returnstring.strip()

    def dig(self, inputstring=None):
        """Dig something, using the tool it needs."""
        if not inputstring:
            return "What should I dig?"
        resolved = self.resolve(inputstring)
        if type(resolved) == str:
            return resolved
        else:
            object_, _, _, _ = resolved
        if not (
            "digable" in object_.properties and object_.properties["digable"] == True
        ):
            return "That's ridiculous."
        if "digtool" in object_.properties:
            if object_.properties["digtool"] not in (
                inv := {
                    obj.properties["object"]: obj
                    for obj in self.person.inventory.holding
                }
            ):
                return (
                    object_.properties["nodigtoolmessage"]
                    if "nodigtoolmessage" in object_.properties
                    else f'I need {self.anifier(object_.properties["digtool"])} to do that...'
                )
            else:
                return (
                    inv[object_.properties["digtool"]].use(self.person, object_),
                    self.updateobjectindex(),
                )[0]
        else:
            return (object_.dig(self.person), self.updateobjectindex())[0]

    def read(self, inputstring=None):
        """Read a note."""
        if not inputstring:
            return "What should I read?"
        resolved = self.resolve(inputstring)
        if type(resolved) == str:
            return resolved
        else:
            object_, _, _, _ = resolved
        if object_.properties["type"] != "note":
            return "That's ridiculous."
        return object_.read()

    def press(self, inputstring=None):
        """Press a button."""
        if not inputstring:
            return "What should I press?"
        resolved = self.resolve(inputstring)
        if type(resolved) == str:
            return resolved
        else:
            object_, _, _, _ = resolved
        if object_.properties["type"] != "button":
            return "That's ridiculous."
        return object_.press(self.person)

    def examine(self, inputstring=None):
        """Describe an object from its properties (size, color, material, contents, message)."""
        if not inputstring:
            return "What should I examine?"
        resolved = self.resolve(inputstring)
        if type(resolved) == str:
            return resolved
        else:
            object_, objectname, _, _ = resolved
        other = object_.properties["other"] if "other" in object_.properties else ""
        otherafter = (
            object_.properties["otherafter"]
            if "otherafter" in object_.properties
            else ""
        )
        color = object_.properties["color"] if "color" in object_.properties else ""
        material = (
            object_.properties["material"] if "material" in object_.properties else None
        )
        height = (
            object_.properties["height"] if "height" in object_.properties else None
        )
        width = object_.properties["width"] if "width" in object_.properties else None
        message = (
            object_.properties["message"] if "message" in object_.properties else None
        )
        holding = (
            (
                object_.holding
                if object_.properties["type"] in ("container", "blocking-container")
                else None
            )
            if "open" not in object_.properties or object_.properties["open"] == True
            else None
        )
        reference = (
            object_.properties["reference"].capitalize()
            if "reference" in object_.properties
            else "It"
        )
        if height:
            firstword = str(height)
        elif width:
            firstword = str(width)
        elif other:
            firstword = other
        elif color:
            firstword = color
        else:
            firstword = objectname
        if "plural" in object_.properties and object_.properties["plural"] == True:
            plural = object_.properties["pluralreference"]
        else:
            plural = False
        returnstring = f'{reference} {"are" if plural else "is"} {(("an" if firstword[0] in ("a", "e", "o", "i", "u") else "a") if not plural else plural) if "pluralreference" not in object_.properties else object_.properties["pluralreference"]} '
        if height:
            if width:
                returnstring += f'{height} {"meters" if height != 1 else "meter"} high and {width} {"meters" if width != 1 else "meter"} wide'
            else:
                returnstring += f'{height} {"meters" if height != 1 else "meter"} high'
        elif width:
            returnstring += f'{width} {"meters" if width != 1 else "meter"} wide'
        if (height or width) and (other or color):
            returnstring += " "
        if other:
            returnstring += other + " "
        if color:
            returnstring += color + " "
        returnstring += objectname
        if otherafter:
            returnstring += ", " + otherafter
        if material:
            materialnoof = (
                "materialnoof" in object_.properties
                and object_.properties["materialnoof"]
            )
            returnstring += f', made{" of" if not materialnoof else ""} {material}.'
        else:
            returnstring += "."
        if holding:
            returnstring += (
                " "
                + object_.properties["objectsare"].capitalize()
                + f' the {objectname if "secondname" not in object_.properties else object_.properties["secondname"]} '
            )
            if len(holding) == 1:
                if (
                    "plural" in next(iter(holding)).properties
                    and next(iter(holding)).properties["plural"] == True
                ):
                    plural = True
                else:
                    plural = False
            else:
                plural = True
            returnstring += "are " if plural else "is "
            returnstring += self.formatplural(
                sorted([obj.properties["object"] for obj in holding])
            )
            returnstring += "."
        if message:
            returnstring += " " + message
        return returnstring

    def give(self, inputstring=None):
        """Give an inventory object to an NPC.
        The NPC's give() returns a tuple:
            (False,)            the NPC refuses
            (False, dialogue)   the NPC refuses but says something
            (True, dialogue)    the NPC accepts (the object is removed) and says something
        """
        if not inputstring:
            return "What should I give to who?"
        if " to " not in inputstring:
            if inputstring in self.objectindex:
                if self.objectindex[inputstring] in self.person.inventory.holding:
                    return "Who should I give it to?"
                else:
                    return "I don't have that."
            else:
                return "I don't have that."
        objectname, personname = inputstring.split(" to ")
        if objectname.startswith("the "):
            objectname = objectname[4:]
        if personname.startswith("the "):
            personname = personname[4:]
        if personname not in self.objectindex:
            return "I don't see that here."
        if objectname not in self.objectindex:
            return "I don't have that."
        object_ = self.objectindex[objectname]
        objectname = object_.properties["object"]
        if object_ not in self.person.inventory.holding:
            return "I don't have that."
        if object_ in self.person.wearing:
            return "Remove it first."
        person = self.objectindex[personname]
        personname = person.properties["object"]
        if person.properties["type"] != "npc":
            return f"I don't think the {personname} wants it."
        answer = person.give(object_, self.person)
        if not answer[0]:
            if len(answer) == 1 or not answer[1]:
                if (
                    "plural" in object_.properties
                    and object_.properties["plural"] == True
                ):
                    plural = True
                else:
                    plural = False
                return f'{person.properties["reference"].capitalize() if "reference" in person.properties else "It"} refuses {"them" if plural else "it"}.'
            else:
                return self.talk(personname, answer[1])
        object_.delete()
        printoutput(
            f"Gave \x1b[1m\x1b[38;5;136m{objectname}\x1b[0m to \x1b[1m\x1b[38;5;136m{personname}\x1b[0m."
        )
        if answer[1]:
            printoutput()
            self.updateobjectindex()
            return self.talk(personname, answer[1])

    def talk(self, personname=None, givendil=None):
        """Talk to an NPC, running its dialogue tree until it ends.
        Args:
            personname: name of the NPC.
            givendil: a dialogue to run instead of asking the NPC (used after give).
        """
        if not personname:
            return "Who should I talk to?"
        if personname.startswith("the "):
            personname = personname[4:]
        if personname not in self.objectindex:
            return "I don't see that here."
        person = self.objectindex[personname]
        personname = person.properties["object"]
        if person.properties["type"] != "npc":
            return f"Hello, {personname}!"
        # curdil is the current [text, next] node of the dialogue tree (see the Game docstring).
        curdil = givendil if givendil else person.dialogues(self.person)
        starting = True
        while True:
            if not starting:
                printoutput()
            else:
                starting = False
            if curdil[1] is None:
                self.updateobjectindex()
                return curdil[0]
            if type(curdil[1]) != dict:
                givenobjectnames = sorted(
                    [obj.properties["object"] for obj in curdil[1]]
                )
                self.updateobjectindex()
                return (
                    f'\x1b[3m{person.properties["reference"].capitalize()} gives me {self.formatplural(givenobjectnames)}.\x1b[0m'
                    + "\n"
                    + curdil[0]
                )
            printoutput(curdil[0] + "\n")
            i = 0
            chosableoptions = []
            for option in curdil[1].keys():
                i += 1
                chosableoptions.append(option)
                printoutput(f"\x1b[3m[{i}] {option}\x1b[0m")
            while True:
                chosen = getinput(
                    f'\n\x1b[3moptions: {"/".join(map(str, range(1, i + 1)))}.\x1b[0m '
                ).strip()
                try:
                    if int(chosen) <= 0:
                        raise Exception
                    chosen = chosableoptions[int(chosen) - 1]
                except Exception:
                    printoutput("That is not a valid option.")
                else:
                    break
            curdil = curdil[1][chosen]

    def enter(self, placename=None):
        """Enter an inside world such as a house or hut."""
        if not placename:
            return "What should I enter?"
        if placename.startswith("in "):
            placename = placename[3:]
        elif placename.startswith("inside "):
            placename = placename[7:]
        elif placename.startswith("into "):
            placename = placename[5:]
        elif placename.startswith("to "):
            placename = placename[3:]
        if placename.startswith("the "):
            placename = placename[4:]
        if placename in self.allworlds():
            return f"I am already in the {placename}!"
        if placename not in self.objectindex:
            return "I don't see that here."
        place = self.objectindex[placename]
        if place.properties["type"] != "inside-world":
            return "That's ridiculous."
        return self.move(placename)

    def allworlds(self):
        """Return the names of every inside world the player is currently in, from the innermost out."""
        toreturn = set()
        curworld = self.world
        while True:
            if curworld.properties["type"] != "inside-world":
                return toreturn
            toreturn.add(curworld.properties["object"])
            if "secondname" in curworld.properties:
                toreturn.add(curworld.properties["secondname"])
            curworld = curworld.position.world

    def move(self, givendir=None):
        """Move the player in a direction, towards an object or path, or into an inside world.
        Blocking objects stop the player, guards get a chance to react and stepping
        onto an exit cell leaves the current inside world. Successful moves also look
        around.
        """
        if not givendir:
            return "Where should I go?"
        if givendir.startswith("in "):
            givendir = givendir[3:]
        elif givendir.startswith("inside "):
            givendir = givendir[7:]
        elif givendir.startswith("into "):
            givendir = givendir[5:]
        elif givendir.startswith("to "):
            givendir = givendir[3:]
        if givendir.startswith("the "):
            givendir = givendir[4:]
        # Work out the target: a compass direction, an inside world, a path or any visible object.
        curpos = self.person.position.worldposition
        if givendir in self.allworlds():
            return f"I am already in the {givendir}!"
        if givendir in ("n", "north"):
            dir_ = "north"
            position = (curpos[0], curpos[1] + 1)
        elif givendir in ("s", "south"):
            dir_ = "south"
            position = (curpos[0], curpos[1] - 1)
        elif givendir in ("w", "west"):
            dir_ = "west"
            position = (curpos[0] - 1, curpos[1])
        elif givendir in ("e", "east"):
            dir_ = "east"
            position = (curpos[0] + 1, curpos[1])
        elif givendir in self.objectindex:
            object_ = self.objectindex[givendir]
            objectname = object_.properties["object"]
            if object_.properties["type"] == "inside-world":
                position = object_.startingpos
                guardingblockingpos = object_
                dir_ = object_.properties["insidereference"] + " " + objectname
            elif object_.properties["type"] == "path":
                pos = self.aroundpaths[objectname][4]
                if pos == (myposition := self.person.position.worldposition):
                    pathstring = "I am already there!"
                else:
                    diffx, diffy = (
                        abs(pos[0] - myposition[0]) / (pos[0] - myposition[0])
                        if pos[0] != myposition[0]
                        else 0
                    ), (
                        abs(pos[1] - myposition[1]) / (pos[1] - myposition[1])
                        if pos[1] != myposition[1]
                        else 0
                    )
                    dir_ = ""
                    if diffy != 0:
                        dir_ += "north" if diffy + 1 else "south"
                        if diffx != 0:
                            dir_ += "-"
                    if diffx != 0:
                        dir_ += "east" if diffx + 1 else "west"
                guardingblockingpos = position = self.world.positions[pos]
            else:
                if object_.position == self.person.inventory:
                    return f'I\'m carrying {object_.properties["reference"] if "reference" in object_.properties else "it"}!'
                if object_.position.properties["type"] != "world-position":
                    return "I can't go there."
                pos = object_.position.worldposition
                myposition = self.person.position.worldposition
                dir_ = ""
                diffx, diffy = (
                    abs(pos[0] - myposition[0]) / (pos[0] - myposition[0])
                    if pos[0] != myposition[0]
                    else 0
                ), (
                    abs(pos[1] - myposition[1]) / (pos[1] - myposition[1])
                    if pos[1] != myposition[1]
                    else 0
                )
                if diffx == 0 and diffy == 0:
                    return "I am already there!"
                if diffy != 0:
                    dir_ += "north" if diffy + 1 else "south"
                    if diffx != 0:
                        dir_ += "-"
                if diffx != 0:
                    dir_ += "east" if diffx + 1 else "west"
                positionpos = (myposition[0] + diffx, myposition[1] + diffy)
                if positionpos[0] in (-1, self.world.size[0]) or positionpos[1] in (
                    -1,
                    self.world.size[1],
                ):
                    return f"I cannot go {dir_} from here."
                position = self.world.positions[positionpos]
                guardingblockingpos = position
            if guardingblockingpos.blocking:
                return f'{self.anifier(self.world.positions[positionpos].blocking.properties["object"]).capitalize()} blocks my way.'
            if guardingblockingpos.guarding:
                guard = guardingblockingpos.guarding
                guardsay = guard.guardtalk(self.person)
                if guardsay[0] == False:
                    return (
                        f'The {guard.properties["object"]} stops me.\n\n'
                        + self.talk(guard.properties["object"], guardsay[1])
                    )
                else:
                    if (
                        self.world.properties["type"] == "inside-world"
                        and position.worldposition in self.world.exitfrom
                    ):
                        return self.exit()
                    self.person.move(position)
                    self.updateobjectindex()
                    printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
                    if object_.properties["type"] == "inside-world":
                        self.world = object_
                    return (
                        self.talk(guard.properties["object"], guardsay[1])
                        + "\n\n"
                        + f"Going \x1b[1m\x1b[38;5;30m{dir_}\x1b[0m...\n\n"
                        + self.lookaround()
                        if guardsay[1] is not None
                        else f"Going \x1b[1m\x1b[38;5;30m{dir_}\x1b[0m...\n\n"
                        + self.lookaround()
                    )
            if (
                self.world.properties["type"] == "inside-world"
                and position.worldposition in self.world.exitfrom
            ):
                return self.exit()
            self.person.move(position)
            if object_.properties["type"] == "inside-world":
                self.world = object_
            self.updateobjectindex()
            printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
            return f"Going \x1b[1m\x1b[38;5;30m{dir_}\x1b[0m...\n\n" + self.lookaround()
        else:
            return "I don't see that here."
        if position[0] in (-1, self.world.size[0]) or position[1] in (
            -1,
            self.world.size[1],
        ):
            return f"I cannot go {dir_} from here."
        if self.world.positions[position].insideworld:
            return self.move(
                self.world.positions[position].insideworld.properties["object"]
            )
        if self.world.positions[position].blocking:
            return f'{self.anifier(self.world.positions[position].blocking.properties["object"]).capitalize()} blocks my way.'
        if self.world.positions[position].guarding:
            guard = self.world.positions[position].guarding
            guardsay = guard.guardtalk(self.person)
            if guardsay[0] == False:
                return f'The {guard.properties["object"]} stops me.\n\n' + self.talk(
                    guard.properties["object"], guardsay[1]
                )
            else:
                if (
                    self.world.properties["type"] == "inside-world"
                    and position in self.world.exitfrom
                ):
                    return self.exit()
                self.person.move(self.world.positions[position])
                self.updateobjectindex()
                printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
                return (
                    self.talk(guard.properties["object"], guardsay[1])
                    + "\n\n"
                    + f"Going \x1b[1m\x1b[38;5;30m{dir_}\x1b[0m...\n\n"
                    + self.lookaround()
                    if guardsay[1] is not None
                    else f"Going \x1b[1m\x1b[38;5;30m{dir_}\x1b[0m...\n\n"
                    + self.lookaround()
                )
        if (
            self.world.properties["type"] == "inside-world"
            and position in self.world.exitfrom
        ):
            return self.exit()
        self.person.move(self.world.positions[position])
        self.updateobjectindex()
        printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
        return f"Going \x1b[1m\x1b[38;5;30m{dir_}\x1b[0m...\n\n" + self.lookaround()

    def resolve(self, inputstring):
        """Turn typed text like 'apple from box' into the object it names.
        Returns:
            (object, object name, holder, holder name) with None for the holder parts
            when no holder was given, or an error message string if nothing matches.
        """
        if " from " in inputstring:
            objectname, holdername = inputstring.split(" from ")
        elif " on " in inputstring:
            objectname, holdername = inputstring.split(" on ")
        elif " in " in inputstring:
            objectname, holdername = inputstring.split(" in ")
        else:
            objectname, holdername = inputstring.strip(), None
        if objectname.startswith("the "):
            objectname = objectname[4:]
        if holdername and holdername.startswith("the "):
            holdername = holdername[4:]
        if not holdername and objectname not in self.objectindex:
            return "I don't see that here."
        if holdername and holdername not in self.objectindex:
            return "I don't see that here."
        if holdername:
            holder = self.objectindex[holdername]
            holdername = holder.properties["object"]
            if "open" in holder.properties and holder.properties["open"] == False:
                return "It's locked!"
            if holder.properties["type"] not in ("container", "blocking-container"):
                return "\x1b[31mSorry, I don't understand.\x1b[0m"
            holding = holder.holding
            objectnamesholding = {}
            for obj in holding:
                objectnamesholding[obj.properties["object"]] = obj
                if "secondname" in obj.properties:
                    objectnamesholding[obj.properties["secondname"]] = obj
            if objectname not in objectnamesholding:
                return f'I don\'t see that {holder.properties["objectsare"]} the \x1b[1m\x1b[38;5;136m{holdername}\x1b[0m.'
            object_ = objectnamesholding[objectname]
            objectname = object_.properties["object"]
        else:
            object_ = self.objectindex[objectname]
            objectname = object_.properties["object"]
            holder = None
            holdername = None
        return (object_, objectname, holder, holdername)

    def take(self, inputstring=None):
        """Pick up an object."""
        if not inputstring:
            return "What should I take?"
        resolved = self.resolve(inputstring)
        if type(resolved) == str:
            return resolved
        else:
            object_, objectname, _, holdername = resolved
        if object_ in self.person.inventory.holding:
            if "plural" in object_.properties and object_.properties["plural"] == True:
                return "I already have them!"
            else:
                return "I already have it!"
        if (
            object_.properties["type"] == "npc"
            and object_.properties["movable"] == False
        ):
            return f"I wouldn't dare try."
        if object_.properties["movable"] == False:
            return "I can't take that."
        self.person.take(object_)
        self.updateobjectindex()
        return (
            f"Took \x1b[1m\x1b[38;5;136m{objectname}\x1b[0m."
            if not holdername
            else f"Took \x1b[1m\x1b[38;5;136m{objectname}\x1b[0m from \x1b[1m\x1b[38;5;136m{holdername}\x1b[0m."
        )

    def put(self, inputstring=None):
        """Put an object in or on something (needs a destination)."""
        if not inputstring:
            return "What should I put where?"
        if (
            " on " not in inputstring
            and " in " not in inputstring
            and " into " not in inputstring
        ):
            return "Where should I put it?"
        return self.drop(inputstring)

    def wear(self, objectname=None):
        """Wear a wearable object from the inventory."""
        if not objectname:
            return "What should I wear?"
        if objectname.startswith("the "):
            objectname = objectname[4:]
        if objectname not in self.objectindex:
            return "I don't have that."
        object_ = self.objectindex[objectname]
        objectname = object_.properties["object"]
        if object_ not in self.person.inventory.holding:
            return "I don't have that."
        if (
            "wearable" not in object_.properties
            or object_.properties["wearable"] == False
        ):
            return "That's ridiculous."
        if object_ in self.person.wearing:
            return "I'm already wearing it!"
        return object_.wear(self.person)

    def unwear(self, objectname=None):
        """Take off a worn object."""
        if not objectname:
            return "What should I take off?"
        if objectname.startswith("the "):
            objectname = objectname[4:]
        if objectname not in self.objectindex:
            return "I don't have that."
        object_ = self.objectindex[objectname]
        objectname = object_.properties["object"]
        if object_ not in self.person.inventory.holding:
            return "I don't have that."
        if (
            "wearable" not in object_.properties
            or object_.properties["wearable"] == False
        ):
            return "That's ridiculous."
        if object_ not in self.person.wearing:
            return "I'm not wearing that."
        return object_.unwear(self.person)

    def drop(self, inputstring=None):
        """Drop an object, or put it in or on a container."""
        if not inputstring:
            return "What should I drop?"
        if " on " in inputstring:
            objectname, newcontainername = inputstring.split(" on ")
        elif " in " in inputstring:
            objectname, newcontainername = inputstring.split(" in ")
        else:
            objectname, newcontainername = inputstring.strip(), None
        if objectname.startswith("the "):
            objectname = objectname[4:]
        if newcontainername and newcontainername.startswith("the "):
            newcontainername = newcontainername[4:]
        if objectname not in self.objectindex:
            return "I don't have that."
        if newcontainername and newcontainername not in self.objectindex:
            return "I don't see that here."
        object_ = self.objectindex[objectname]
        objectname = object_.properties["object"]
        newcontainer = (
            self.objectindex[newcontainername]
            if newcontainername
            else self.person.position
        )
        if object_ not in self.person.inventory.holding:
            return "I don't have that."
        if object_ == newcontainer:
            return f"The \x1b[1m\x1b[38;5;136m{objectname}\x1b[0m warps and drops into itself as the world ends..."
        if (
            "open" in newcontainer.properties
            and newcontainer.properties["open"] == False
        ):
            return "It's locked!"
        if newcontainer.properties["type"] not in (
            "container",
            "world-position",
            "blocking-container",
        ):
            return "I can't do that."
        if (
            "putinside" in newcontainer.properties
            and newcontainer.properties["putinside"] == False
        ):
            return "I can't do that."
        if object_ in self.person.wearing:
            return "Remove it first."
        self.person.drop(object_, newcontainer)
        self.updateobjectindex()
        return (
            f"Dropped \x1b[1m\x1b[38;5;136m{objectname}\x1b[0m."
            if not newcontainername
            else f'Dropped \x1b[1m\x1b[38;5;136m{objectname}\x1b[0m {newcontainer.properties["objectsare"]} \x1b[1m\x1b[38;5;136m{newcontainername}\x1b[0m.'
        )

    def ride(self, objectname=None):
        """Ride a rideable object."""
        if not objectname:
            return "What should I ride?"
        if objectname.startswith("the "):
            objectname = objectname[4:]
        if objectname not in self.objectindex:
            return "I don't see that here."
        object_ = self.objectindex[objectname]
        objectname = object_.properties["object"]
        if "rideable" in object_.properties and object_.properties["rideable"] == True:
            return self.use(objectname)
        else:
            return "That's ridiculous."

    def use(self, inputstring=None):
        """Use an object, optionally on another object ('use x on y')."""
        if not inputstring:
            return "What should I use?"
        if " on " in inputstring:
            objectxname, objectyname = inputstring.split(" on ")
        else:
            objectxname, objectyname = inputstring, None
        if objectxname.startswith("the "):
            objectxname = objectxname[4:]
        if objectyname and objectyname.startswith("the "):
            objectyname = objectyname[4:]
        if objectxname not in self.objectindex:
            return "I don't see that here."
        if objectyname and objectyname not in self.objectindex:
            return "I don't see that here."
        objectx = self.objectindex[objectxname]
        if "usable" in objectx.properties and objectx.properties["usable"] == True:
            pass
        else:
            return "That's ridiculous."
        objecty = self.objectindex[objectyname] if objectyname else None
        if objecty:
            objectsusableon = (
                objectx.properties["objectsusableon"]
                if "objectsusableon" in objectx.properties
                else {objectyname}
            )
            if objectyname in objectsusableon:
                return (
                    objectx.use(object_=objecty, person=self.person),
                    self.updateobjectindex(),
                )[0]
            else:
                return "I can't do that."
        else:
            return (objectx.use(person=self.person), self.updateobjectindex())[0]

    def updateobjectindex(self):
        """Rebuild the lists of what the player can currently refer to.
        Covers the cells around the player (and the contents of open containers there),
        the paths nearby and the inventory. Must be called after anything moves.
        """
        self.aroundobjects = {}
        self.aroundpaths = {}
        self.objectindex = {}
        # Map every cell on a path to that path's details.
        paths = {}
        for path in self.world.paths:
            a = path.a
            b = path.b
            dir_ = path.direction
            paths.update(
                {
                    pos: (path, a, b, dir_)
                    for pos in {
                        (x, y)
                        for x in (
                            range(a[0], b[0] + 1)
                            if b[0] > a[0]
                            else range(b[0], a[0] + 1)
                        )
                        for y in (
                            range(a[1], b[1] + 1)
                            if b[1] > a[1]
                            else range(b[1], a[1] + 1)
                        )
                    }
                }
            )
        # Look at the player's cell and its eight neighbours.
        for xi in (-1, 1, 0):
            for yi in (-1, 1, 0):
                x, y = self.person.position.worldposition
                if not x + xi in (-1, self.person.position.world.size[0]):
                    x += xi
                else:
                    continue
                if not y + yi in (-1, self.person.position.world.size[1]):
                    y += yi
                else:
                    continue
                if (x, y) in paths:
                    path = paths[(x, y)][0]
                    self.aroundpaths[path.properties["object"]] = paths[(x, y)] + (
                        (x, y),
                    )
                    self.objectindex[path.properties["object"]] = path
                    if "secondname" in path.properties:
                        self.objectindex[path.properties["secondname"]] = path
                objects = self.person.position.world.positions[(x, y)].holding
                for object_ in objects:
                    key = object_.properties["object"]
                    secondkey = (
                        object_.properties["secondname"]
                        if "secondname" in object_.properties
                        else None
                    )
                    if key == "person":
                        continue
                    if secondkey == "person":
                        continue
                    self.aroundobjects[key] = (object_, (xi, yi))
                    self.objectindex[key] = object_
                    if secondkey:
                        self.objectindex[secondkey] = object_
                    if object_.properties["type"] in (
                        "container",
                        "blocking-container",
                    ) and (
                        "open" not in object_.properties
                        or object_.properties["open"] == True
                    ):
                        for obj in object_.holding:
                            self.aroundobjects[obj.properties["object"]] = (
                                obj,
                                [object_, (xi, yi)],
                            )
                            self.objectindex[obj.properties["object"]] = obj
                            if "secondname" in obj.properties:
                                self.objectindex[obj.properties["secondname"]] = obj
        # The inventory (and the contents of open containers in it) is always reachable.
        self.inventoryobjects = {}
        for object_ in self.person.inventory.holding:
            key = object_.properties["object"]
            secondkey = (
                object_.properties["secondname"]
                if "secondname" in object_.properties
                else None
            )
            self.inventoryobjects[key] = object_
            self.objectindex[key] = object_
            if secondkey:
                self.objectindex[secondkey] = object_
            if object_.properties["type"] in ("container", "blocking-container") and (
                "open" not in object_.properties or object_.properties["open"] == True
            ):
                for obj in object_.holding:
                    self.objectindex[obj.properties["object"]] = obj
                    if "secondname" in obj.properties:
                        self.objectindex[obj.properties["secondname"]] = obj

    def parse(self, line):
        """Match a typed line to a command and run it.
        Commands may be several words ('pick up'), and everything after the command
        word is passed to its handler as input.
        Returns:
            The text (or EndGame) produced by the command. Unknown input gives a
            'Sorry, I don't understand.' message.
        """
        line = line.strip()
        if not line:
            return "Time passes..."
        words = line.split()
        command = None
        inputs = []
        ongoingcommand = None
        # Greedily build a command (possibly several words) from the words; the rest is its input.
        for word in words:
            if not word:
                continue
            word = word.strip().lower()
            potentialongoingcommand = (
                ongoingcommand + " " + word if not ongoingcommand is None else word
            )
            if potentialongoingcommand in self.ongoingcommands:
                ongoingcommand = potentialongoingcommand
            elif potentialongoingcommand in self.commands:
                if command:
                    return "\x1b[31mSorry, I don't understand.\x1b[0m"
                command = self.commands[potentialongoingcommand]
                ongoingcommand = None
            elif command:
                inputs.append(word)
            elif ongoingcommand in self.commands:
                inputs.append(word)
                command = self.commands[ongoingcommand]
                ongoingcommand = None
            else:
                return "\x1b[31mSorry, I don't understand.\x1b[0m"
        if ongoingcommand in self.commands:
            if command:
                return "\x1b[31mSorry, I don't understand.\x1b[0m"
            command = self.commands[ongoingcommand]
        if not command:
            return "\x1b[31mSorry, I don't understand.\x1b[0m"
        try:
            return command(" ".join(inputs)) if inputs else command()
        except Exception:
            return "\x1b[31mSorry, I don't understand.\x1b[0m"

    def loop(self):
        """Run the game: read commands, show results and handle winning, losing and running out of moves.
        Each iteration of the outer loop is one round; it restarts the game when the
        player chooses to play again.
        """
        while True:
            while True:
                # One typed command per iteration; an EndGame result finishes the round.
                command = getinput("\n> ")
                printoutput()
                output = self.parse(command)
                if type(output) == EndGame:
                    if not output.description and not output.win:
                        printoutput(output.endmessage)
                        return
                    printoutput("\x1b[H\x1b[2J\x1b[3J", end="")
                    printoutput(
                        "\x1b[3m"
                        + output.description
                        + "\x1b[0m\n\n"
                        + output.endmessage
                        + "\n\n"
                        + (
                            "\x1b[1m\x1b[32mYOU WIN!\x1b[0m"
                            if output.win
                            else "\x1b[1m\x1b[31mYOU LOSE.\x1b[0m"
                        )
                        + "\n"
                    )
                    while True:
                        playagain = (
                            getinput(
                                "\x1b[1m\x1b[32mDo you want to play again? (yes/no): \x1b[0m"
                            )
                            .strip()
                            .lower()
                        )
                        if playagain in ("yes", "no"):
                            break
                        else:
                            printoutput("That is not a valid option.\n")
                    if playagain == "yes":
                        break
                    else:
                        printoutput("\nbye")
                        return
                if output:
                    printoutput(output)
                    if 0 < self.maxcommands - self.donecommands - 1 <= 20:
                        printoutput(
                            f'\n\n\x1b[31mYou only have {self.maxcommands - self.donecommands - 1} {"commands" if self.maxcommands - self.donecommands - 1 != 1 else "command"} left!\x1b[0m'
                        )
                    elif self.maxcommands - self.donecommands - 1 == 0:
                        printoutput(f"\n\n\x1b[31mYou have 0 commands left!\x1b[0m")
                    if command.strip().lower() not in (
                        "moves",
                        "commands",
                    ) and output not in (
                        "\x1b[31mSorry, I don't understand.\x1b[0m",
                        "Time passes...",
                    ):
                        self.donecommands += 1
                        if self.donecommands == self.maxcommands:
                            printoutput(
                                "\n\x1b[31mOh no! It is too late. Your rivals have come back to the island and destroyed the device! You have now lost your hard work forever.\x1b[0m\n"
                            )
                            while True:
                                playagain = (
                                    getinput(
                                        "\x1b[1m\x1b[32mDo you want to play again? (yes/no): \x1b[0m"
                                    )
                                    .strip()
                                    .lower()
                                )
                                if playagain in ("yes", "no"):
                                    break
                                else:
                                    printoutput("That is not a valid option.\n")
                            if playagain == "yes":
                                break
                            else:
                                printoutput("\nbye")
                                return
            self.setup()


# The five passwords hidden on the island. All must be entered, each only once.
PASSWORDS = ("ad6x29z", "xz50op3", "g8k9vbn", "8dkr6e9", "b4n7cc1")
LAST_PASSWD_CODE = "Bangalore is Four distances North in Seven of China's largest Cuisines, number One."

queued_inputs = []
inputs = []

# Start the gameloop to play.
if __name__ == "__main__":
    game = Game()
    game.setup()
    if options["load"]:
        game.parse("load")
    game.loop()
