#!/usr/bin/env python3
import os

import pygame as pg


# pygame setup
def main():
    pg.init()

    sx, sy = 1280, 720
    screen = pg.display.set_mode((sx, sy))
    clock = pg.time.Clock()
    cx, cy = 0, 0
    mx, my = 0, 0
    pixelsize = 20
    zoom = 1

    schedule = []
    with open(f"../m5out/dramsim3.schedule.txt") as f:
        print(f.readline())
        for line in f:
            entry = line.strip().split(";")
            schedule.append(
                (int(entry[0]), int(entry[1]), entry[2], int(entry[3]))
            )

    colors = {0: pg.Color("red3"), 1: pg.Color("royalblue3")}

    running = True
    while running:
        # poll for events
        for event in pg.event.get():
            if event.type == pg.QUIT:
                running = False
            elif event.type == pg.KEYDOWN:
                if event.key == pg.K_w:
                    my -= 100
                elif event.key == pg.K_s:
                    my += 100
                if event.key == pg.K_a:
                    mx -= 100
                elif event.key == pg.K_d:
                    mx += 100
            elif event.type == pg.KEYUP:
                if event.key == pg.K_w:
                    my += 100
                elif event.key == pg.K_s:
                    my -= 100
                if event.key == pg.K_a:
                    mx += 100
                elif event.key == pg.K_d:
                    mx -= 100

        cx += mx
        cy += my

        # fill the screen with a color to wipe away anything from last frame
        screen.fill("white")

        # render loop
        for i, entry in enumerate(schedule):
            width = pixelsize * zoom
            height = pixelsize * zoom // 4
            ex = i * width - cx
            ey1 = entry[0] * height - cy
            ey2 = entry[1] * height - cy
            col = colors[entry[3]]
            pg.draw.line(
                screen,
                col,
                (ex + width // 2, ey1),
                (ex + width // 2, ey2),
                width,
            )

        # flip() the display to put your work on screen
        pg.display.flip()

        # limits FPS to 60
        clock.tick(60)

    pg.quit()


if __name__ == "__main__":
    main()
