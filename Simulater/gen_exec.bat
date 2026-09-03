rmdir /s /q build
rmdir /s /q dist    
pyinstaller --noconfirm --onefile --windowed --name MeterSimulator --icon=meter_sim.ico gui_main.py

