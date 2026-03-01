# result/plot_results.py
import matplotlib.pyplot as plt
import numpy as np

# Чтение данных
cwnd_data = np.loadtxt('result/data/cwnd.data', skiprows=1)

# Создание графиков
fig, ax1 = plt.subplots(1, figsize=(12, 8))

# График окна перегрузки
ax1.plot(cwnd_data[:, 0], cwnd_data[:, 1])
ax1.set_xlabel('Время (с)')
ax1.set_ylabel('Размер окна (сегменты)')
ax1.set_title('Окно перегрузки TCP BBR')
ax1.grid(True)

plt.tight_layout()
plt.savefig('result/graph/tcp_bbr_analysis.png', dpi=300)
plt.show()