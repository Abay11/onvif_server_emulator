import tkinter as tk
from tkinter import filedialog

import configparser
import os.path

def browse_file():
	dirname = filedialog.askdirectory()
	pathLabel.config(text=dirname)
	global userServerConfigsDir
	userServerConfigsDir = dirname

def connect_server():
	print('connecting')

def disconnect_server():
	print('disconnect')

root = tk.Tk()
root.minsize(400, 300)

browseServerConfigsFrame = tk.Frame(root)
browseServerConfigsFrame.pack()

browseButton = tk.Button(browseServerConfigsFrame, text="Select configs dir", command=browse_file)
browseButton.pack(side=tk.LEFT)

pathLabel = tk.Label(browseServerConfigsFrame)
pathLabel.pack(side=tk.LEFT)

# restore user configuration
appConfig = configparser.ConfigParser()
appConfigFileName = 'configurator.ini'
configFilesList = appConfig.read(appConfigFileName)
userServerConfigsDir = "Select server configs directory"
if len(configFilesList) != 0:
	appConfig.read(appConfigFileName)
	userServerConfigsDir = appConfig.get('OnvifServerEmulator', 'serverConfigs')
	pathLabel.config(text=userServerConfigsDir)
else:
	appConfig.add_section('OnvifServerEmulator')

# create a menu bar
menu_bar = tk.Menu(root)
root.config(menu=menu_bar)

# create a settings menu
settings_menu = tk.Menu(menu_bar, tearoff=False)
menu_bar.add_cascade(label="Server", menu=settings_menu)

# add items to the settings menu
settings_menu.add_command(label="Connect")
settings_menu.add_command(label="Disconnect")
settings_menu.add_separator()
settings_menu.add_command(label="Exit", command=root.quit)

status_bar = tk.Label(root, text="Status: not connected", bd=1, relief=tk.SUNKEN, anchor=tk.W)
status_bar.pack(side=tk.BOTTOM, fill=tk.X)


buildTreeButton = tk.Button(root, text='Build tree')
buildTreeButton.pack()

root.mainloop()

#save current user configuration 
appConfig.set('OnvifServerEmulator', 'serverConfigs', userServerConfigsDir)
with open(appConfigFileName, 'w') as f:
    appConfig.write(f)