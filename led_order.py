import csv

LOOPS = 4
LEDS_PER_STRIP = 30
EXTENSION_STRIP = 16

def decomment(csvfile):
    for row in csvfile:
        raw = row.split('#')[0].strip()
        if raw: yield raw

def translate_strip(physical_segment, direction, logical_segment):
    leds = []
    start = LEDS_PER_STRIP * logical_segment
    if EXTENSION_STRIP != -1 and logical_segment >= EXTENSION_STRIP:
        start += LEDS_PER_STRIP
    if direction:
        for i in range(LEDS_PER_STRIP):
            leds.append(start + i)
    else:
        for i in range(LEDS_PER_STRIP-1, -1, -1):
            leds.append(start + i)
    return leds

with open('led_order.csv') as csvfile:
    led_table = []
    reader = csv.reader(decomment(csvfile))
    for row in reader:
        led_table += translate_strip(int(row[0]), int(row[1]), int(row[2]))

    output = ""
    LOOP_SIZE = int(len(led_table) / LOOPS)
    for i in range(LOOPS):
        for j in range(LOOP_SIZE):
            output += '%d, ' % led_table[i * LOOP_SIZE + j]
        output += '\n'
    print(output)
            
            



