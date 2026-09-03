from PIL import Image

img = Image.open("meter_sim_source.png")  # 정사각형, 고해상도 원본
sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
img.save("meter_sim.ico", format="ICO", sizes=sizes)

