from matplotlib import cm
import numpy
import scm

n = 512

cmaps = [cm.twilight_shifted, cm.magma, cm.bone, cm.CMRmap, cm.Spectral]
cmaps = [scm.cmap(x) for x in scm.available]
names = [cmap.name.lower() for cmap in cmaps]

assert len(set(names)) == len(names)

for name, cmap in zip(names, cmaps):
    if hasattr(cmap, "colors"):
        # Listed colomaps
        colors = (numpy.array(cmap.colors) * 255 + 0.5).astype(numpy.uint8)
    else:
        colors = (cmap(numpy.linspace(0, 1, n))[:, :3] * 255 + 0.5).astype(numpy.uint8)
    print(f"\nconst uint8_t {name}[] = {{")
    print(",\n".join(f"    {r}, {g}, {b}" for r, g, b in colors))
    print(f"}};\n")

print(f"""
    const uint8_t* colormaps[] = {{{", ".join(names)}}};
    const char* colormap_names[] = {{"{'", "'.join(names)}"}};
    const int colormap_sizes[] = {{COUNT({"), COUNT(".join(names)})}};""")
