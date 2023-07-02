import tkinter as tk
from tkinter import filedialog

import configparser
import os.path

def browse_file():
	dirname = filedialog.askdirectory()
	pathLabel.config(text=dirname)
	global userServerConfigsDir
	userServerConfigsDir = dirname

root = tk.Tk()

pathLabel = tk.Label(root)
pathLabel.pack()

browseButton = tk.Button(root, text="Browse", command=browse_file)
browseButton.pack()

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

root.mainloop()

#save current user configuration 
appConfig.set('OnvifServerEmulator', 'serverConfigs', userServerConfigsDir)
with open(appConfigFileName, 'w') as f:
    appConfig.write(f)