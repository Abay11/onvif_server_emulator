from tkinter import filedialog
import os.path
import configparser

APP_CONFIG_FILENAME = 'configurator.ini'
onvifServerConfigsDir = '' 
appConfig = configparser.ConfigParser()

def load():
	# restore user configuration
	global appConfig
	global onvifServerConfigsDir 
	configFilesList = appConfig.read(APP_CONFIG_FILENAME)
	if len(configFilesList) != 0:
		appConfig.read(APP_CONFIG_FILENAME)
		if 'OnvifServerEmulator' in appConfig.keys():
			onvifServerConfigsDir = appConfig.get('OnvifServerEmulator', 'serverConfigs')

def save():
	global appConfig
	global onvifServerConfigsDir
	if 'OnvifServerEmulator' not in appConfig.keys():
		appConfig.add_section('OnvifServerEmulator')
	appConfig.set('OnvifServerEmulator', 'serverConfigs', onvifServerConfigsDir)
	with open (APP_CONFIG_FILENAME, 'w') as file:
		appConfig.write(file)

def select_server_configs_dir():
	global onvifServerConfigsDir
	while len(onvifServerConfigsDir) == 0:
		onvifServerConfigsDir = filedialog.askdirectory(title='Please set Onvif Server configs directory')
