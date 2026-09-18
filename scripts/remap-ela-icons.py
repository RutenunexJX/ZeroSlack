"""Remap ElaWidgetTools' referenced icons to Font Awesome Free 6.7.2."""

from pathlib import Path
import re

root = Path(__file__).resolve().parents[1] / "thirdparty" / "ElaWidgetTools"
header = root / "ElaWidgetToolsDef.h"

# Values come from Font Awesome Free 6.7.2 metadata/icons.json. Icons absent
# from the free solid font use the closest free glyph, so every icon rendered
# inside the vendored library has a documented source license.
codes = {
    "AngleDown": "f107", "AngleLeft": "f104", "AngleRight": "f105",
    "AngleUp": "f106", "AnglesDown": "f103", "AnglesLeft": "f100",
    "AnglesRight": "f101", "AnglesUp": "f102",
    "ArrowDownToLine": "f019", "ArrowLeft": "f060",
    "ArrowLeftToLine": "f060", "ArrowRight": "f061",
    "ArrowRightToLine": "f061", "ArrowRotateLeft": "f0e2",
    "ArrowRotateRight": "f01e", "ArrowUpToLine": "f093",
    "ArrowsRotate": "f021", "Bars": "f0c9", "Broom": "f51a",
    "CalendarRange": "f073", "CaretDown": "f0d7", "CaretUp": "f0d8",
    "Check": "f00c", "Copy": "f0c5", "Dash": "f068",
    "DeleteLeft": "f55a", "GearComplex": "f013",
    "KnifeKitchen": "f2e7", "ListTree": "f03a", "ListUl": "f0ca",
    "LocationArrow": "f124", "MagnifyingGlass": "f002",
    "Minus": "f068", "MoonStars": "f186", "ObjectGroup": "f247",
    "Paste": "f0ea", "Pencil": "f303", "Plus": "002b",
    "RotateLeft": "f2ea", "RotateRight": "f2f9", "Square": "f0c8",
    "SunBright": "f185", "Thumbtack": "f08d",
    "UpDownLeftRight": "f0b2", "UpRightFromSquare": "f35d",
    "WindowRestore": "f2d2", "Xmark": "f00d",
}

source = header.read_bytes().decode("utf-8")
start = source.index("enum IconName")
end = source.index("Q_END_ENUM_CREATE(ElaIconType)")
prefix, enum, suffix = source[:start], source[start:end], source[end:]
for name, code in codes.items():
    pattern = rf"(?m)^(\s*{re.escape(name)}\s*=\s*)0x[0-9a-fA-F]+(,?)"
    enum, count = re.subn(pattern, rf"\g<1>0x{code}\g<2>", enum)
    if count != 1:
        raise RuntimeError(f"Expected one {name} enum entry, got {count}")
header.write_bytes((prefix + enum + suffix).encode("utf-8"))

for path in root.rglob("*.cpp"):
    data = path.read_bytes()
    updated = data.replace(b'"ElaAwesome"', b'"Font Awesome 6 Free"')
    updated = updated.replace(b'"Font Awesome 6 Free Solid"',
                              b'"Font Awesome 6 Free"')
    updated = updated.replace(
        b':/include/Font/ElaAwesome.ttf',
        b':/include/Font/FontAwesomeFreeSolid.ttf',
    )
    if updated != data:
        path.write_bytes(updated)
