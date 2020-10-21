LED_PER_SEGMENT = 4

class Loop:

    def __init__(self):
        self.__segments = []

    def get_segments(self):
        return self.__segments
    
    def add_segment(self, segment):
        self.__segments.append(segment)

    def get_segment_addresses(self):
        addresses = []
        for seg in self.__segments:
            addresses.extend(seg.get_addresses())
        return str(addresses)

class Segment:

    def __init__(self, direction, index):
        self.__direction = direction
        self.__index = int(index)

    def get_addresses(self):
        addresses = []
        start = 0
        end = 0
        if self.__direction:
            start = self.__index * LED_PER_SEGMENT
            end = (self.__index + 1) * LED_PER_SEGMENT
        else:
            start = (self.__index + 1) * LED_PER_SEGMENT - 1
            end = self.__index * LED_PER_SEGMENT - 1

        for i in range(start, end):
            addresses.append(i)
        return addresses

    def __str__(self):
        return "Segment: {} Direction: {}".format(self.__index, self.__direction)

def create_loops_formatted(loops):
    formatted_lump = ""
    for loop in loops:
        addresses = loop.get_segment_addresses().replace("[", "").replace("]", "") + ","
        addresses += '\n'
        formatted_lump += addresses
    return formatted_lump

def save_loops(loop_string):
    file = open("addresses.txt", "w+")
    file.write(loop_string)
    file.close()

filename = "led_test_addresses.csv"

raw_data = ""

try:
    file = open(filename, "r")
    raw_data = file.read()
    file.close()
except Exception as e:
    print(e)

data_lines = raw_data.splitlines()

loops = []

for line in data_lines:
    if len(line) > 1:
        if line[0] is not "#":
            data = line.strip().replace(" ", "").split(",")
            print(data)
            seg = Segment(bool(data[1]), data[2])
            #print(str(seg))
            if not len(loops) > 0:
                loops.append(Loop())
            loops[-1].add_segment(seg)
        else:
            print("Comment Line")        
    else:
        loops.append(Loop())
loop_string = create_loops_formatted(loops)
print(loop_string)
save_loops(loop_string)
#for loop in loops:
#    print(loop.get_segment_addresses())