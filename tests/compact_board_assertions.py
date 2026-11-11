"""Measure compact board pixels independently of the viewport's text label."""
import re
from collections import defaultdict
from test_ftxui_menu import pixels, raw_rows


def assert_compact_boards(terminal, rows, columns, panels, monochrome=False):
    raster = {y: pixels(row) for y, row in raw_rows(terminal).items()}
    groups = defaultdict(list)
    if monochrome:
        # Monochrome didactic boards have a light border and uncompressed rows.
        for y, line in raster.items():
            text = ''.join(p[0] for p in line)
            for match in re.finditer(r'┌(─+)┐', text):
                width = len(match[1])
                if width not in (columns, columns * 2):
                    continue
                x = match.start()
                bottom = raster.get(y + rows + 1, [])
                assert len(bottom) > x + width + 1, (y, x, rows)
                assert bottom[x][0] == '└' and bottom[x + width + 1][0] == '┘'
                for offset in range(1, rows + 1):
                    body = raster[y + offset]
                    assert body[x][0] == body[x + width + 1][0] == '│'
                    assert all(p[0] == ' ' for p in body[x + 1:x + width + 1])
                groups[(x, width, False)].append(y)
        assert len(groups) == panels, (groups, terminal.screen())
        return
    for y, line in raster.items():
        first = None
        for x, pixel in enumerate(line + [(None, None, None)]):
            # Menu focus and panel fills are not board cells.
            cell = ((pixel[0] == ' ' or (pixel[0] == '▀' and pixel[1] is not None)) and pixel[2] is not None
                    and pixel[2] not in ((83, 191, 208), (25, 33, 43), (25, 32, 38)))
            if cell and first is None:
                first = x
            if not cell and first is not None:
                run = line[first:x]
                half = all(p[0] == '▀' for p in run)
                expected = columns if half else columns * 2
                if len(run) == expected:
                    groups[(first, len(run), half)].append(y)
                first = None
    boards = []
    for (x, width, half), ys in groups.items():
        expected = (rows + 1) // 2 if half else rows
        # Every advertised row must really be drawn, with no gaps or clipping.
        if len(ys) == expected and ys == list(range(ys[0], ys[0] + expected)):
            boards.append((x, ys[0], width, expected))
    assert len(boards) == panels, (rows, columns, panels, boards, groups, terminal.screen())
