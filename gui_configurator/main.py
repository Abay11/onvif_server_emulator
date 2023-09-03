import tkinter as tk

import user_configs

def connect_server():
	print('connecting')

def disconnect_server():
	print('disconnect')

def main():
	root = tk.Tk()
	root.minsize(400, 300)

	user_configs.load()

	if len(user_configs.onvifServerConfigsDir) == 0:
		user_configs.select_server_configs_dir()
		user_configs.save()
	
	root.title(user_configs.onvifServerConfigsDir)

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

	root.mainloop()

if __name__ == "__main__":
	main()