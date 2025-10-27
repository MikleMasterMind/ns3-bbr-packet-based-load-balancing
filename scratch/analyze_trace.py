import re
import sys
import argparse
from datetime import datetime
from collections import defaultdict

def parse_trace_line(line):
    """
    Улучшенный парсер для строк трассировки NS-3.
    Обрабатывает различные форматы TCP заголовков и событий.
    """
    # Основные компоненты для разбора
    event_pattern = r'^([r+\-])'  # событие: r, +, -
    time_pattern = r'\s+(\d+\.\d+)'  # время
    node_pattern = r'\s+/NodeList/(\d+)/'  # номер узла
    
    # Более гибкое извлечение длины пакета
    length_pattern = r'length:\s*(\d+)'
    
    # Разные варианты TCP заголовков
    tcp_patterns = [
        # Стандартный формат с Seq
        r'Seq=(\d+)',
        # Формат с квадратными скобками и флагами
        r'\[([^\]]+)\].*Seq=(\d+)',
        # Минимальный формат - просто ищем числа после Seq=
        r'.*Seq=(\d+)',
    ]
    
    # Сначала извлекаем базовую информацию
    base_match = re.match(f'{event_pattern}{time_pattern}{node_pattern}.*{length_pattern}', line)
    if not base_match:
        return None
    
    event_type, time_str, node_str, length_str = base_match.groups()
    
    # Ищем TCP последовательность и флаги
    seq_number = None
    tcp_flags = []
    
    for pattern in tcp_patterns:
        tcp_match = re.search(pattern, line)
        if tcp_match:
            groups = tcp_match.groups()
            if len(groups) == 1:
                seq_number = int(groups[0])
            elif len(groups) == 2:
                tcp_flags = groups[0].split('|')
                seq_number = int(groups[1])
            break
    
    # Если не нашли Seq, попробуем найти по другому шаблону
    if seq_number is None:
        # Ищем числа в контексте TCP
        numbers_match = re.search(r'TcpHeader.*?(\d+)\s*>\s*(\d+)', line)
        if numbers_match:
            # Берем первый номер как последовательность (упрощенно)
            seq_number = int(numbers_match.group(1))
    
    # Если все еще нет последовательности, используем хэш строки как fallback
    if seq_number is None:
        seq_number = hash(line) % 1000000
    
    # Определяем тип пакета по содержимому
    packet_type = "Data"
    if any(flag in line for flag in ['[SYN]', 'SYN']):
        packet_type = "SYN"
        tcp_flags.append('SYN')
    elif any(flag in line for flag in ['[FIN]', 'FIN']):
        packet_type = "FIN" 
        tcp_flags.append('FIN')
    elif any(flag in line for flag in ['[RST]', 'RST']):
        packet_type = "RST"
        tcp_flags.append('RST')
    elif 'ACK' in line and int(length_str) <= 60:
        packet_type = "ACK"
        tcp_flags.append('ACK')
    
    return {
        'event_type': event_type,
        'time': float(time_str),
        'node_id': int(node_str),
        'packet_size': int(length_str),
        'seq': seq_number,
        'flags': tcp_flags,
        'packet_type': packet_type,
        'raw_line': line.strip()  # Сохраняем исходную строку для отладки
    }

def analyze_tcp_flags(flags_list):
    """Анализирует TCP флаги."""
    flags_dict = {
        'SYN': any('SYN' in flag for flag in flags_list),
        'ACK': any('ACK' in flag for flag in flags_list),
        'FIN': any('FIN' in flag for flag in flags_list),
        'RST': any('RST' in flag for flag in flags_list),
        'PSH': any('PSH' in flag for flag in flags_list),
    }
    return flags_dict

def analyze_trace(input_file, output_file):
    """
    Анализирует файл трассировки и собирает метрики.
    """
    total_packets = 0
    retransmitted_packets = 0
    total_bytes = 0
    start_time = None
    end_time = 0
    
    # Для отслеживания потоков и ретрансмитов
    flows = defaultdict(lambda: {'seen_seqs': set(), 'packet_count': 0})
    packet_types = defaultdict(int)
    events_by_type = defaultdict(int)
    
    # Статистика для отладки
    parsed_lines = 0
    failed_lines = 0
    problematic_lines = []

    with open(input_file, 'r') as f:
        for line_num, line in enumerate(f, 1):
            if not line.strip():
                continue

            data = parse_trace_line(line)
            if not data:
                failed_lines += 1
                if len(line.strip()) > 10:
                    problematic_lines.append((line_num, line.strip()[:100]))
                continue

            parsed_lines += 1
            events_by_type[data['event_type']] += 1

            # Обновляем временные рамки
            if start_time is None:
                start_time = data['time']
            end_time = max(end_time, data['time'])

            # Анализ TCP флагов
            flags_info = analyze_tcp_flags(data['flags'])
            
            # Классификация пакетов по типу
            packet_types[data['packet_type']] += 1

            # Учитываем только пакеты с данными для некоторых метрик
            if data['packet_type'] in ['Data', 'ACK']:
                total_packets += 1
                total_bytes += data['packet_size']

                # Создаем идентификатор потока
                flow_id = f"node_{data['node_id']}_seq_{data['seq']}"
                
                # Проверка на повторную передачу (упрощенно)
                if data['seq'] in flows[flow_id]['seen_seqs']:
                    retransmitted_packets += 1
                    packet_types['Retransmissions'] += 1
                else:
                    flows[flow_id]['seen_seqs'].add(data['seq'])
                    flows[flow_id]['packet_count'] += 1

    # Расчет метрик
    duration = end_time - start_time if start_time else 0
    speed_bps = (total_bytes * 8) / duration if duration > 0 else 0
    
    # Дополнительные метрики
    unique_flows = len(flows)
    avg_packets_per_flow = total_packets / unique_flows if unique_flows > 0 else 0
    retransmission_rate = (retransmitted_packets / total_packets * 100) if total_packets > 0 else 0

    # Запись результатов
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(f"Анализ трассировки: {input_file}\n")
        f.write(f"Дата анализа: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        
        f.write(f"\n--- СТАТИСТИКА ОБРАБОТКИ ---\n")
        f.write(f"Успешно обработано строк: {parsed_lines}\n")
        f.write(f"Не удалось обработать: {failed_lines}\n")
        f.write(f"Процент успешной обработки: {parsed_lines/(parsed_lines+failed_lines)*100:.1f}%\n")
        
        f.write(f"\n--- РАСПРЕДЕЛЕНИЕ СОБЫТИЙ ---\n")
        for event_type, count in events_by_type.items():
            f.write(f"{event_type}: {count} событий\n")
        
        f.write(f"\n--- ОСНОВНЫЕ МЕТРИКИ ---\n")
        f.write(f"Общее число пакетов с данными: {total_packets}\n")
        f.write(f"Число повторных передач: {retransmitted_packets}\n")
        f.write(f"Уровень ретрансмиссии: {retransmission_rate:.2f}%\n")
        f.write(f"Общий объём данных: {total_bytes} байт ({total_bytes/1024:.2f} KB)\n")
        f.write(f"Длительность передачи: {duration:.3f} сек\n")
        f.write(f"Средняя скорость: {speed_bps:.2f} бит/сек ({speed_bps/1e6:.2f} Мбит/сек)\n")
        
        f.write(f"\n--- СТАТИСТИКА ПО ТИПАМ ПАКЕТОВ ---\n")
        for pkt_type, count in packet_types.items():
            f.write(f"{pkt_type}: {count} пакетов\n")
        
        f.write(f"\n--- ИНФОРМАЦИЯ О ПОТОКАХ ---\n")
        f.write(f"Уникальных потоков: {unique_flows}\n")
        f.write(f"Среднее пакетов на поток: {avg_packets_per_flow:.1f}\n")
        
        # Добавляем информацию о проблемных строках для отладки
        if problematic_lines:
            f.write(f"\n--- ПРОБЛЕМНЫЕ СТРОКИ (первые 10) ---\n")
            for i, (line_num, line_content) in enumerate(problematic_lines[:10]):
                f.write(f"Строка {line_num}: {line_content}...\n")

    print(f"Анализ завершен. Результаты сохранены в {output_file}")
    print(f"Обработано: {parsed_lines} строк, Проблемных: {failed_lines}")
    print(f"Уровень успешной обработки: {parsed_lines/(parsed_lines+failed_lines)*100:.1f}%")

def main():
    parser = argparse.ArgumentParser(description="Анализ трассировки NS-3")
    parser.add_argument("input_file", help="Входной файл трассировки (например, bbr-experiment.tr)")
    parser.add_argument("-o", "--output", default="analysis_results.txt", 
                       help="Выходной файл для результатов (по умолчанию: analysis_results.txt)")
    
    args = parser.parse_args()

    try:
        analyze_trace(args.input_file, args.output)
    except FileNotFoundError:
        print(f"Ошибка: Файл {args.input_file} не найден")
        sys.exit(1)
    except Exception as e:
        print(f"Ошибка при анализе: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()