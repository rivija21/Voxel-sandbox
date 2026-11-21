"""Builds voxel-guide.html (the web version of the study guide) from README.md and docs/*.md.

    python docs\\web\\build_guide.py

Run it after editing any .md file. It only reads the Markdown; it writes voxel-guide.html.

Files in this folder:
    template.html       page design (CSS + JavaScript); the Markdown is pasted into it
    textures.raw        the game's 15 block textures, used as icons (16x16 RGBA each)
    dump_textures.cpp   regenerates textures.raw after you change src/textures.cpp:
                          g++ -std=c++17 docs\\web\\dump_textures.cpp src\\textures.cpp src\\blocks.cpp -o dump.exe
                          dump.exe docs\\web\\textures.raw
    img/                screenshots shown at the top of the page
"""
import json
import re
import struct
import zlib
from pathlib import Path
import base64

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent  # docs/web -> docs -> project folder

TEX_NAMES = ["GrassTop", "GrassSide", "Dirt", "Stone", "Sand", "Water", "LogSide", "LogTop",
             "Leaves", "Planks", "Glass", "Brick", "Cobble", "Snow", "Bedrock"]
TEX_LABELS = ["Grass top", "Grass side", "Dirt", "Stone", "Sand", "Water", "Log side", "Log top",
              "Leaves", "Planks", "Glass", "Brick", "Cobble", "Snow", "Bedrock"]

CHAPTERS = [  # id, file, short name, icon
    ("start", "README.md", "Start here", "GrassSide"),
    ("ch01", "docs/01-setup-and-build.md", "Setup and build", "Planks"),
    ("ch02", "docs/02-architecture.md", "Architecture", "Brick"),
    ("ch03", "docs/03-cpp-semantics.md", "C++ semantics", "Cobble"),
    ("ch04", "docs/04-windows-and-input.md", "Windows and input", "Glass"),
    ("ch05", "docs/05-opengl-pipeline.md", "OpenGL pipeline", "Water"),
    ("ch06", "docs/06-3d-math.md", "3D maths", "Stone"),
    ("ch07", "docs/07-world-generation.md", "World generation", "GrassTop"),
    ("ch08", "docs/08-meshing-and-lighting.md", "Meshing and lighting", "Leaves"),
    ("ch09", "docs/09-physics-and-raycasting.md", "Physics and raycasting", "Sand"),
    ("ch10", "docs/10-exercises.md", "Exercises", "LogTop"),
    ("ch11", "docs/11-glossary.md", "Glossary", "Snow"),
]


def png_rgba(width, height, rgba):
    rows = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def textures():
    raw = (HERE / "textures.raw").read_bytes()
    size = 16 * 16 * 4
    out = {}
    for i, name in enumerate(TEX_NAMES):
        png = png_rgba(16, 16, raw[i * size:(i + 1) * size])
        out[name] = {"src": "data:image/png;base64," + base64.b64encode(png).decode(), "label": TEX_LABELS[i]}
    return out


def suspicious_html(md, name):
    """Warn about '<' outside code, which a Markdown renderer would treat as an HTML tag."""
    in_fence = False
    for n, line in enumerate(md.splitlines(), 1):
        if line.startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        outside = re.sub(r"`[^`]*`", "", line)
        if re.search(r"<[A-Za-z/!]", outside):
            print(f"  check {name}:{n}: {line.strip()[:90]}")


def chapters():
    out = []
    for cid, rel, short, tex in CHAPTERS:
        md = (PROJECT / rel).read_text(encoding="utf-8")
        first, rest = md.split("\n", 1)
        assert first.startswith("# "), rel
        heading = first[2:].strip()
        m = re.match(r"(\d\d) - (.*)", heading)
        num, title = (m.group(1), m.group(2)) if m else ("", "Start here")
        suspicious_html(rest, rel)
        out.append({"id": cid, "num": num, "title": title, "short": short, "tex": tex, "md": rest})
    return out


def js_json(value):
    text = json.dumps(value, ensure_ascii=False)
    return text.replace("</", "<\\/").replace("<!--", "<\\!--")


template = (HERE / "template.html").read_text(encoding="utf-8")
page = template.replace("__TEX_JSON__", js_json(textures())).replace("__CHAPTERS_JSON__", js_json(chapters()))
(HERE / "voxel-guide.html").write_text(page, encoding="utf-8")
print(f"wrote voxel-guide.html ({len(page.encode('utf-8')) // 1024} KB)")
