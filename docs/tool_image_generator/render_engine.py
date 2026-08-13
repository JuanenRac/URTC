#!/usr/bin/env python3
"""
Motor de renderizado de diagramas de herramientas URTC.
Genera TOOL_<NOMBRE>.png a partir de PCB.png (referencia vacia, 5 ID pins)
mas un panel de texto con la info de conectores/pines/notas de cada herramienta.
"""
from PIL import Image, ImageDraw, ImageFont
import os

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
PCB_PATH = os.path.join(BASE_DIR, "PCB.png")

# --- Coordenadas verificadas por pixel-analysis contra la imagen real ---
CONNECTOR_BOXES = {
    "CONN_LED2":  (195, 10, 355, 148),
    "CONN_SEN":   (390, 10, 595, 148),
    "CONN_LED1":  (630, 10, 785, 148),
    "CONN_OLED":  (818, 10, 1020, 148),
    "CONN_FAN1":  (1058, 148, 1162, 380),
    "CONN_MOT":   (26, 163, 200, 463),
    "CONN_MAIN":  (26, 693, 200, 988),
    "CONN_DRILL": (1028, 588, 1162, 995),
    "CONN_T12":   (780, 966, 1025, 1160),
    # Bounding box ampliado a la derecha para incluir la etiqueta rotada
    # "CONN_EXPANSION" al dibujar la caja naranja de resaltado.
    "CONN_EXPANSION": (555, 597, 855, 935),
}

# Los 5 cuadrados ID, en orden ID4..ID0 (bit4..bit0), izquierda a derecha
ID_PIN_BOXES = [
    ("ID4", (553, 205, 598, 267), 0x10),
    ("ID3", (600, 205, 645, 267), 0x08),
    ("ID2", (647, 205, 692, 267), 0x04),
    ("ID1", (694, 205, 739, 267), 0x02),
    ("ID0", (741, 205, 786, 267), 0x01),
]

# CONN_EXPANSION: 20 pines (1.27mm, 2x10), dibujado verticalmente debajo de MS1/MS2.
# No existe en la imagen de referencia original - añadido a peticion del usuario.
# Numeracion zigzag estandar de header/IDC: impares columna izq (1,3,5..19),
# pares columna der (2,4,6..20) - pin1 arriba-izquierda, junto a la muesca de polarizacion.
EXPANSION_PIN_R = 8
EXPANSION_COL_GAP = 46
EXPANSION_ROW_GAP = 30
EXPANSION_TOP_Y = 645
EXPANSION_CENTER_X = 626
EXPANSION_LEFT_X = EXPANSION_CENTER_X - EXPANSION_COL_GAP // 2
EXPANSION_RIGHT_X = EXPANSION_CENTER_X + EXPANSION_COL_GAP // 2
EXPANSION_BOX = (
    EXPANSION_LEFT_X - 24, EXPANSION_TOP_Y - 28,
    EXPANSION_RIGHT_X + 24, EXPANSION_TOP_Y + 9 * EXPANSION_ROW_GAP + 20,
)
CONNECTOR_BOXES_EXTRA_LABEL_WIDTH = 165  # espacio que ocupa la etiqueta rotada a la derecha, para la caja naranja

def draw_conn_expansion(draw):
    """Dibuja el conector CONN_EXPANSION de 20 pines. Se llama siempre,
    en las 25 imagenes, igual que MS1/MS2 - la caja naranja de resaltado
    se añade aparte (ver CONNECTOR_BOXES) solo para las herramientas que
    realmente lo necesitan."""
    f_pin = font(12, bold=True)
    f_label = font(20, bold=True)

    pin_positions = {}
    for row in range(10):
        y = EXPANSION_TOP_Y + row * EXPANSION_ROW_GAP
        pin_positions[row * 2 + 1] = (EXPANSION_LEFT_X, y)
        pin_positions[row * 2 + 2] = (EXPANSION_RIGHT_X, y)

    box_x0, box_y0, box_x1, box_y1 = (
        EXPANSION_LEFT_X - 24, EXPANSION_TOP_Y - 20,
        EXPANSION_RIGHT_X + 24, EXPANSION_TOP_Y + 9 * EXPANSION_ROW_GAP + 20,
    )
    draw.rounded_rectangle([box_x0, box_y0, box_x1, box_y1], radius=6, outline=BLACK, width=3)
    draw.rectangle([EXPANSION_CENTER_X - 10, box_y0 - 8, EXPANSION_CENTER_X + 10, box_y0], outline=BLACK, width=2)

    for pin_num, (x, y) in pin_positions.items():
        draw.ellipse([x - EXPANSION_PIN_R, y - EXPANSION_PIN_R, x + EXPANSION_PIN_R, y + EXPANSION_PIN_R], fill=BLACK)
        if pin_num % 2 == 1:
            tw = draw.textlength(str(pin_num), font=f_pin)
            draw.text((x - EXPANSION_PIN_R - tw - 4, y - 7), str(pin_num), fill=BLACK, font=f_pin)
        else:
            draw.text((x + EXPANSION_PIN_R + 4, y - 7), str(pin_num), fill=BLACK, font=f_pin)

    # Etiqueta rotada, en su propia capa RGBA para poder rotarla limpiamente
    label_img = Image.new("RGBA", (260, 30), (255, 255, 255, 0))
    ld = ImageDraw.Draw(label_img)
    ld.text((0, 0), "CONN_EXPANSION", fill=BLACK, font=f_label)
    label_rot = label_img.rotate(90, expand=True)
    return label_rot, (box_x1 + 15, (box_y0 + box_y1) // 2 - label_rot.height // 2)

ORANGE = (230, 126, 34)
GREEN = (39, 174, 96)
BLACK = (0, 0, 0)
GRAY = (110, 110, 110)

FONT_DIR = "/usr/share/fonts/truetype/dejavu"
def font(size, bold=False, italic=False):
    name = "DejaVuSans"
    if bold and italic:
        name += "-BoldOblique"
    elif bold:
        name += "-Bold"
    elif italic:
        name += "-Oblique"
    return ImageFont.truetype(os.path.join(FONT_DIR, f"{name}.ttf"), size)

def draw_pcb_diagram(tool_id, used_connectors, expansion_needed=False):
    """Dibuja el diagrama de la PCB con highlights, devuelve la imagen PIL."""
    img = Image.open(PCB_PATH).convert("RGB")
    draw = ImageDraw.Draw(img)

    # CONN_EXPANSION se dibuja siempre (como MS1/MS2), no formaba parte de
    # la imagen de referencia original - añadido a peticion del usuario.
    label_rot, label_pos = draw_conn_expansion(draw)
    img.paste(label_rot, label_pos, label_rot)

    # Cajas naranjas alrededor de los conectores usados
    for name in used_connectors:
        if name not in CONNECTOR_BOXES:
            continue
        x0, y0, x1, y1 = CONNECTOR_BOXES[name]
        draw.rounded_rectangle([x0-6, y0-6, x1+6, y1+6], radius=8, outline=ORANGE, width=5)

    # Jumpers ID: verde + "S" si el bit correspondiente esta activo en tool_id
    id_font = font(26, bold=True)
    for label, (x0, y0, x1, y1), bitmask in ID_PIN_BOXES:
        if tool_id & bitmask:
            draw.rectangle([x0+3, y0+3, x1-3, y1-3], fill=GREEN, outline=BLACK, width=3)
            tw = draw.textlength("S", font=id_font)
            draw.text(((x0+x1)/2 - tw/2, (y0+y1)/2 - 16), "S", fill=(255,255,255), font=id_font)

    return img

def wrap_text(draw, text, fnt, max_width):
    words = text.split(" ")
    lines = []
    cur = ""
    for w in words:
        test = (cur + " " + w).strip()
        if draw.textlength(test, font=fnt) <= max_width:
            cur = test
        else:
            if cur:
                lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines

print("motor de renderizado cargado OK")

# --- Renderizado del panel de texto debajo del diagrama ---
PAGE_WIDTH = 1223
MARGIN = 40
LINE_GAP = 6

def measure_text_panel(tool):
    """Calcula la altura total necesaria del panel de texto antes de crearlo."""
    # Usamos una imagen temporal solo para medir texto
    tmp = Image.new("RGB", (PAGE_WIDTH, 10))
    d = ImageDraw.Draw(tmp)
    y = 30  # padding superior

    f_title = font(34, bold=True)
    f_h2 = font(19, bold=True)
    f_h3 = font(18, bold=True)
    f_body = font(15)
    f_body_i = font(15)
    f_small = font(13)
    f_note = font(14, bold=True, italic=True)

    # Titulo
    y += 44
    # ID jumpers a soldar
    y += 26
    # "Universal connectors" header
    y += 30
    for name, desc, pins in tool["universal"]:
        y += 24  # nombre conector
        wrapped = wrap_text(d, desc, f_body, PAGE_WIDTH - 2*MARGIN - 20)
        y += len(wrapped) * 20
        y += 4
        y += len(pins) * 20
        y += 14
    # separador + "Connectors specific" header
    if tool["specific"]:
        y += 20
        y += 30
    for name, desc, pins in tool["specific"]:
        y += 24
        wrapped = wrap_text(d, desc, f_body, PAGE_WIDTH - 2*MARGIN - 20)
        y += len(wrapped) * 20
        y += 4
        y += len(pins) * 20
        y += 14
    # notas
    if tool.get("notes"):
        y += 20
        y += 26
        for note in tool["notes"]:
            wrapped = wrap_text(d, note, f_note, PAGE_WIDTH - 2*MARGIN)
            y += len(wrapped) * 20
            y += 6
    # expansion note
    if tool.get("expansion_note"):
        y += 20
        y += 26
        wrapped = wrap_text(d, tool["expansion_note"], f_note, PAGE_WIDTH - 2*MARGIN)
        y += len(wrapped) * 20
        y += 6

    y += 30  # padding inferior
    return y

def render_text_panel(tool, panel_height):
    img = Image.new("RGB", (PAGE_WIDTH, panel_height), (255, 255, 255))
    d = ImageDraw.Draw(img)
    y = 30

    f_title = font(34, bold=True)
    f_h2 = font(19, bold=True)
    f_h3 = font(18, bold=True)
    f_body = font(15)
    f_small = font(13)
    f_note = font(14, bold=True, italic=True)
    f_jumpers = font(16, bold=True)

    # Titulo: "0x0A - 3D PRINTER"
    title = f"0x{tool['id']:02X} - {tool['name']}"
    d.text((MARGIN, y), title, fill=BLACK, font=f_title)
    y += 44

    # ID jumpers a soldar
    bits = tool["id"]
    active = [lbl for lbl, _, mask in ID_PIN_BOXES if bits & mask]
    if active:
        jtext = "ID jumpers to solder: " + ", ".join(active)
        color = GREEN
    else:
        jtext = "ID jumpers to solder: NONE (all 5 jumpers left open)"
        color = GRAY
    d.text((MARGIN, y), jtext, fill=color, font=f_jumpers)
    y += 30

    # Universal connectors
    d.text((MARGIN, y), "Universal connectors (present on every tool)", fill=BLACK, font=f_h2)
    y += 30
    for name, desc, pins in tool["universal"]:
        d.ellipse([MARGIN, y+6, MARGIN+10, y+16], fill=ORANGE)
        d.text((MARGIN+18, y), name, fill=BLACK, font=f_h3)
        y += 24
        wrapped = wrap_text(d, desc, f_body, PAGE_WIDTH - 2*MARGIN - 20)
        for line in wrapped:
            d.text((MARGIN+18, y), line, fill=(90,90,90), font=f_body)
            y += 20
        y += 4
        for pin_line in pins:
            d.text((MARGIN+30, y), pin_line, fill=BLACK, font=f_body)
            y += 20
        y += 14

    y += 6
    d.line([(MARGIN, y), (PAGE_WIDTH-MARGIN, y)], fill=(200,200,200), width=1)
    y += 14

    # Specific connectors
    if tool["specific"]:
        d.text((MARGIN, y), "Connectors specific to this tool", fill=BLACK, font=f_h2)
        y += 30
    for name, desc, pins in tool["specific"]:
        d.ellipse([MARGIN, y+6, MARGIN+10, y+16], fill=ORANGE)
        d.text((MARGIN+18, y), name, fill=BLACK, font=f_h3)
        y += 24
        wrapped = wrap_text(d, desc, f_body, PAGE_WIDTH - 2*MARGIN - 20)
        for line in wrapped:
            d.text((MARGIN+18, y), line, fill=(90,90,90), font=f_body)
            y += 20
        y += 4
        for pin_line in pins:
            d.text((MARGIN+30, y), pin_line, fill=BLACK, font=f_body)
            y += 20
        y += 14

    # Notas
    if tool.get("notes"):
        y += 6
        d.line([(MARGIN, y), (PAGE_WIDTH-MARGIN, y)], fill=(200,200,200), width=1)
        y += 14
        d.text((MARGIN, y), "Notes", fill=BLACK, font=f_h2)
        y += 26
        for note in tool["notes"]:
            wrapped = wrap_text(d, note, f_note, PAGE_WIDTH - 2*MARGIN)
            for line in wrapped:
                d.text((MARGIN, y), line, fill=(140,60,10), font=f_note)
                y += 20
            y += 6

    # Nota de tarjeta de expansion (conector no visible en esta vista de la placa)
    if tool.get("expansion_note"):
        y += 6
        d.line([(MARGIN, y), (PAGE_WIDTH-MARGIN, y)], fill=(200,200,200), width=1)
        y += 14
        d.text((MARGIN, y), "Expansion board required", fill=BLACK, font=f_h2)
        y += 26
        wrapped = wrap_text(d, tool["expansion_note"], f_note, PAGE_WIDTH - 2*MARGIN)
        for line in wrapped:
            d.text((MARGIN, y), line, fill=(140,60,10), font=f_note)
            y += 20

    return img

def build_tool_image(tool):
    """Ensambla el diagrama + panel de texto en una sola imagen final."""
    diagram = draw_pcb_diagram(tool["id"], tool["used_connectors"])
    panel_h = measure_text_panel(tool)
    text_panel = render_text_panel(tool, panel_h)

    total_h = diagram.height + text_panel.height
    final = Image.new("RGB", (PAGE_WIDTH, total_h), (255,255,255))
    final.paste(diagram, (0, 0))
    final.paste(text_panel, (0, diagram.height))
    return final
