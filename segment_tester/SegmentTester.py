###########################################
# Zoetrope segment tester code            #
# pySerial is required to run this code!  #
###########################################
from sys import byteorder
from pip._vendor.six import byte2int
from telnetlib import theNULL


version = "alpha"
ERROR_PREFIX = "[!] Oops: " #prints before any error messages

# ------------------- #
#  IMPORT MODULES     #
# ------------------- #

import glob
import atexit
import io
import struct
from tkinter import *
from tkinter import ttk
from threading import Thread
try:
    import serial
except:
    print(ERROR_PREFIX + "PySerial is not installed on your system")


# --------------------------- #
# Global Variable Decleration #
# --------------------------- #

# if there is a serial connection
serial_connected = False

# if the serial connection needs to start
run_serial = False


# This variable does something
readin_glob = 0

# Default baud speed. And yes, WE ARE RUNNING WELL OVER 9000!
default_baud = 19200

# ------------------- #
#  FIND SERIAL PORTS  #
# ------------------- #


def update_serial_list(): # updates the list of serial ports

    def get_serial_list(): # returns a list of serial ports available
        
        # Find possible ports
        if sys.platform.startswith('win'):
            ports = ['COM%s' % (i + 1) for i in range(256)]
        elif sys.platform.startswith('linux') or sys.platform.startswith('cygwin'):
            ports = glob.glob('/dev/tty[A-Za-z]*')
        elif sys.platform.startswith('darwin'):
            ports = glob.glob('/dev/tty.*')
        else:
            print(ERROR_PREFIX + "Your operating system is not supported (try Windows, Mac, or Linux)")
        
        # Filter ports
        result = []
        for port in ports:
            try:
                s = serial.Serial(port)
                s.close()
                result.append(port)
            except (OSError, serial.SerialException):
                pass
        return result

    serial_port_list = get_serial_list()
    print(serial_port_list)
    
    try:
        serial_port_selected.set(serial_port_list[0])
    except:
        print(ERROR_PREFIX + "No serial ports") # if there is no first item, no serial ports exist


# ------------------- #
#  OPEN SERIAL PORT   #
# ------------------- #

ser = serial.Serial() # create a serial object

 
# whether the port is open
serial_connected = False


run_serial = False # whether serial is being read in

def ser_start():
    
    global serial_connected
    serial_connected = False
   
    serial_port = serial_port_selected.get() # get the selected serial port
    ser.port = serial_port

    serial_baud = baud_selected.get() # get the selected baud rate
    ser.baudrate = serial_baud
    
    ser.timeout = None
    
    try:
        ser.open() # Open Port
        print("Serial port {} opened, with a baudrate of {}".format(serial_port, serial_baud)) # Prints opened serial port, and the baudrate
        serial_connected = True
        ser_run()
        toggle_start_button()
    except:
        print(ERROR_PREFIX + "Failed to open/read the serial port at {}, at {} baud".format(serial_port, serial_baud))
        ser.close()
        serial_connected = False



# ----------------- #
# Serial Run Method #
# ----------------- #
        
def ser_run():
    
    global run_serial
    run_serial = True

# ------------------ #
# Serial Stop Method #
# ------------------ #

def ser_stop():
    
    global run_serial
    run_serial = False
    
    ser.close()
    toggle_start_button()
    
    global serial_connected
    serial_connected = False


# |---------------------------------|
# |--#############################--|
# |             TKINTER             |
# |--#############################--|
# |---------------------------------|

# Create a window
root = Tk()
root.title("Zoetrope Tester ({})".format(version))
root.resizable(0,0)

class SectionFrame(ttk.LabelFrame):
    def __init__(self, name, rowNumber, columnNumber, colSpan):
        self.frame = ttk.LabelFrame(root, text=name)
        self.frame.grid(row=rowNumber, column=columnNumber, sticky="nswe", columnspan=colSpan, padx=10, pady=10)


# |--#############--|
# |    1: SERIAL    |
# |--#############--|

### PAGE 1 INIT ###
serialFrame = SectionFrame("Serial Port", 0, 0, 1)

# Prepare list for drop down
serial_port_list = []
serial_port_selected = StringVar() #create a variable for selected value
update_serial_list()
print(serial_port_list)
print(serial_port_selected.get())

# Create drop down selector
serial_port_optionmenu = ttk.OptionMenu(serialFrame.frame, serial_port_selected, "",*serial_port_list)
serial_port_optionmenu.grid(row=0, column=0, sticky=W+E+N+S)

# Create a refresh button
serial_refresh_button = ttk.Button(serialFrame.frame, text="Refresh", command=update_serial_list)
serial_refresh_button.grid(row=0, column=1, sticky=W+E+N+S)

def reset_serial_baud():
    global default_baud
    baud_selected.set(default_baud) # default value

# Create a baudrate selector
baud_selected = StringVar()
standard_baud = (50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200) # possible baudrates
baud_spinbox = Spinbox(serialFrame.frame, textvariable=baud_selected, values=standard_baud, validate="all", state="readonly")
reset_serial_baud()
baud_spinbox.grid(row=1, column=0, sticky=W+E+N+S)
print(baud_selected.get())

# Create a reset button
serial_reset_button = ttk.Button(serialFrame.frame, text="Reset", command=reset_serial_baud)
serial_reset_button.grid(row=1, column=1, sticky=W+E+N+S)

# Toggle function for start/stop button
def toggle_start_button():
    if start_button["text"] == "Start": # serial is not being read
        start_button.config(text="Stop", command=ser_stop)
    else: # serial is being read
        start_button.config(text="Start", command=ser_start)

#Create start/stop button
start_button = ttk.Button(serialFrame.frame, text="Start", command = ser_start)
start_button.grid(row=2, column=0, columnspan=2, sticky=W+E+N+S, pady=5)

# ---------------- #
#  COMMAND WINDOW  #
# ---------------- #

CalibrationFrame = SectionFrame("Debug", 1, 0, 2)

# Segment Debugging
segment_selected = StringVar()
segments = tuple(range(0, 24))
segments_dropdown = ttk.OptionMenu(CalibrationFrame.frame, segment_selected, "", *segments)
segments_dropdown.grid(row=1, column=1, sticky=W+E+N+S)
print(segment_selected.get())
Label(CalibrationFrame.frame, text="Segment: ").grid(row=1)

segment_reverse = IntVar()
segment_reverse_checkbox = Checkbutton(CalibrationFrame.frame, text="Reverse Segment", variable=segment_reverse, onvalue="1", offvalue="0")
segment_reverse_checkbox.grid(row=1, column=2)


# ------------------- #
#  MAIN LOOP          #
# ------------------- #

def main():

    while True:
        
        found_beginning = False

        if run_serial is True:

            try:
                while ser.in_waiting > 0:
                    readin = ser.readline().strip().decode('ascii')
                    print(readin)
                    
            except Exception as ex:
                # Print Exception
                print(ex)
                print(ERROR_PREFIX + "nothing to read")
                # Close Serial Port
                #ser_stop()

main_process = Thread(target=main)

# sets thread to daemon so that it gets terminated when the main thread gets ended
main_process.daemon = True
main_process.start()
root.mainloop() # Open Window
