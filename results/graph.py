# result/plot_results.py
import matplotlib.pyplot as plt
import numpy as np

# Чтение данных
cwnd_data = np.loadtxt('results/data/cwnd.data', skiprows=1)

# Создание графиков
fig, ((ax1)) = plt.subplots(1, figsize=(12, 8))

# График окна перегрузки
ax1.plot(cwnd_data[:, 0], cwnd_data[:, 1])
ax1.set_xlabel('Время (с)')
ax1.set_ylabel('Размер окна (байты)')
ax1.set_title('Окно перегрузки LTCP')
ax1.grid(True)

# # Гистограмма распределения размера окна
# ax4.hist(cwnd_data[:, 1], bins=50, alpha=0.7)
# ax4.set_xlabel('Размер окна (сегменты)')
# ax4.set_ylabel('Частота')
# ax4.set_title('Распределение размера окна')
# ax4.grid(True)

plt.tight_layout()
plt.savefig('results/graph/ltcp_cubic_analysis.png', dpi=300)
plt.show()