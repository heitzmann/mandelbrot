# Mandelbrot

Mandelbrot fractal image generator.
Usefull to create random wallpapers.

## Usage

```sh
Usage: ./mandelbrot [OPTION]... FILENAME

Options:
  -h                    Show this help message and exit.
  -g WIDTH HEIGHT       Image size (defaults 960 540).
  -c X Y                View window center.
  -s DX DY              View window size.
  -z MIN MAX            Minimal value to accept a random coordinate as image
                        center and maximal value for the fractal calculation
                        (defaults 128 2048).
  -m COLORMAP           Colormap name. Available options:
                        acton, bamako, batlow, berlin, bilbao, broc, broco,
                        buda, cork, corko, davos, devon, grayc, hawaii,
                        imola, lajolla, lapaz, lisbon, nuuk, oleron, oslo,
                        roma, romao, tofino, tokyo, turku, vik, viko
  -r RNG_SEED           Random number generator seed.
  -p NUM                Number of threads to use.
```

## Examples

![Image examples](/examples.png "Image examples")
