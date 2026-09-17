#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Ahmet Efe Nezli
# TF02-Pro LiDAR ROS 2 node - sensor_msgs/Range yayinlar
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Range
import serial


class LidarNode(Node):
    def __init__(self):
        super().__init__('hss_lidar_node')

        # Parametreler (varsayilanlar)
        self.declare_parameter('port', '/dev/ttyUSB0')
        self.declare_parameter('baud', 115200)
        self.declare_parameter('frame_id', 'lidar_link')

        port = self.get_parameter('port').value
        baud = self.get_parameter('baud').value
        self.frame_id = self.get_parameter('frame_id').value

        # Seri port
        try:
            self.ser = serial.Serial(port, baud, timeout=1)
            self.get_logger().info(f'TF02-Pro baglandi: {port} @ {baud}')
        except Exception as e:
            self.get_logger().error(f'Seri port acilamadi: {e}')
            raise

        # Yayinci
        self.pub = self.create_publisher(Range, '/lidar/range', 10)

        # 50 Hz oku (TF02 100 Hz'e kadar yayinlar)
        self.timer = self.create_timer(0.02, self.oku_ve_yayinla)

    def oku_ve_yayinla(self):
        # Cerceve basligi: 0x59 0x59
        if self.ser.read(1) != b'\x59':
            return
        if self.ser.read(1) != b'\x59':
            return
        cerceve = self.ser.read(7)
        if len(cerceve) != 7:
            return

        # Checksum dogrula (opsiyonel ama guvenli)
        toplam = (0x59 + 0x59 + sum(cerceve[:6])) & 0xFF
        if toplam != cerceve[6]:
            return  # bozuk cerceve, atla

        dist_cm = cerceve[0] + (cerceve[1] << 8)
        strength = cerceve[2] + (cerceve[3] << 8)

        # Gecersiz okuma filtrele (sinyal cok dusuk veya cok yuksek)
        if strength < 100 or dist_cm == 0:
            return

        # Range mesaji (ROS metre ister)
        msg = Range()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = self.frame_id
        msg.radiation_type = Range.INFRARED
        msg.field_of_view = 0.0349  # ~2 derece (TF02 dar isin)
        msg.min_range = 0.1         # 10 cm
        msg.max_range = 22.0        # TF02-Pro ~22 m
        msg.range = dist_cm / 100.0 # cm -> m
        self.pub.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = LidarNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.ser.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
