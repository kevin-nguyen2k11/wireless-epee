import threading
import time
import sys
import time
import traceback
import signal
import socket
from PyQt6 import QtNetwork, QtCore, QtSerialPort
from PyQt6.QtMultimedia import QSoundEffect
from PyQt6.QtGui import QFont
from PyQt6.QtCore import Qt,QTimer,QObject,QRunnable,pyqtSignal,QThreadPool,pyqtSlot,QUrl
from PyQt6.QtWidgets import (QLabel,QApplication,QMainWindow,QWidget,QVBoxLayout,QGridLayout,QPushButton)

class SignalWatchdog(QtNetwork.QAbstractSocket):
    def __init__(self):
        """ Propagates system signals from Python to QEventLoop """
        super().__init__(QtNetwork.QAbstractSocket.SocketType.SctpSocket,None)
        self.writer,self.reader = socket.socketpair()
        self.writer.setblocking(False)
        signal.set_wakeup_fd(self.writer.fileno())  # Python hook
        self.setSocketDescriptor(self.reader.fileno())  # Qt hook
        self.readyRead.connect(lambda: None)  # Dummy function call

class ColorSquare(QWidget):
    def __init__(self,color):
        super().__init__()
        self.label=QLabel("0")
        self.counter=0
        self.new_color=color
        self.label.setStyleSheet("background-color: black; color: white;")
        self.label.setFont(QFont("Arial",100))
        self.label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.palette=self.label.palette()

    def change_color(self):
        self.label.setStyleSheet("background-color: {};".format(self.new_color))

    def reset_color(self):
        self.label.setStyleSheet("background-color: black; color: white")

    def increment(self):
        self.counter+=1
        self.label.setText(str(self.counter))

    def decrement(self):
        if self.counter>0:
            self.counter-=1
            self.label.setText(str(self.counter))
    
    def reset_score(self):
        self.counter=0
        self.label.setText(str(self.counter))

class ScoreboardWindow(QMainWindow):
    def __init__(self,width,height,beep):
        super().__init__()
        self.width=width
        self.height=height
        self.beep=beep
        self.setWindowTitle("Fencing Scoreboard")
        self.general_layout=QVBoxLayout()
        central_widget=QWidget(self)
        central_widget.setLayout(self.general_layout)
        self.setCentralWidget(central_widget)
        self.create_lights()
        self.create_adjust_buttons()
        self.create_reset_button()
        self.timer=QTimer(self)
        self.timer.setTimerType(Qt.TimerType.PreciseTimer)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self.reset_hits)
        self.is_hit=[False,False]
        # self.server=Server(self)
        # self.server.start_server()
        # self.server.hit.connect(self.on_hit)
        self.setup_serial()

    def create_lights(self):
        layout=QGridLayout()
        self.boxes=[ColorSquare("red"),ColorSquare("green")]
        for i in range(2):
            self.boxes[i].label.setMaximumHeight(int(self.height*0.9))
            layout.addWidget(self.boxes[i].label,0,i)
        self.general_layout.addLayout(layout)

    def create_adjust_buttons(self):
        layout=QGridLayout()
        buttons=[QPushButton("-"),QPushButton("+"),QPushButton("-"),QPushButton("+")]
        for i in range(4):
            buttons[i].setStyleSheet("background-color: black; color: white;")
            buttons[i].setFont(QFont("Arial",45))
            if i%2:
                buttons[i].clicked.connect(self.boxes[i//2].increment)
            else:
                buttons[i].clicked.connect(self.boxes[i//2].decrement)
            layout.addWidget(buttons[i],0,i)
        self.general_layout.addLayout(layout)

    def create_reset_button(self):
        layout=QVBoxLayout()
        button=QPushButton("Reset")
        button.setStyleSheet("background-color: black; color: white;")
        button.setFont(QFont("Arial",45))
        button.clicked.connect(self.reset_click)
        layout.addWidget(button)
        self.general_layout.addLayout(layout)
    
    def reset_click(self):
        for i in range(2):
            self.boxes[i].reset_score()

    def setup_serial(self):
        self.serial=QtSerialPort.QSerialPort()
        self.serial.setPortName('/dev/cu.usbserial-0001')
        self.serial.setBaudRate(115200)
        self.serial.setDataBits(QtSerialPort.QSerialPort.DataBits.Data8)
        self.serial.setParity(QtSerialPort.QSerialPort.Parity.NoParity)
        self.serial.setStopBits(QtSerialPort.QSerialPort.StopBits.OneStop)
        self.serial.setFlowControl(QtSerialPort.QSerialPort.FlowControl.NoFlowControl)
        self.serial.open(QtCore.QIODeviceBase.OpenModeFlag.ReadOnly)
        self.serial.setDataTerminalReady(True)
        time.sleep(2)
        self.serial.clear()
        self.serial.readyRead.connect(self.on_hit)
    
    @pyqtSlot()
    def on_hit(self):
        i=int(self.serial.read(1).decode('utf-8'))
        if not self.is_hit[i]:
            if self.timer.isActive():
                if self.timer.remainingTime()<1950:
                    return
            else:
                self.timer.start(2000)
                self.beep.play()
            self.boxes[i].change_color()
            self.boxes[i].increment()
            self.is_hit[i]=True

    def reset_hits(self):
        for i in range(2):
            self.serial.clear()
            self.boxes[i].reset_color()
            self.is_hit[i]=False

class Server(QObject):
    hit=pyqtSignal(int)

    def __init__(self,window):
        QObject.__init__(self)
        self.window=window
        self.ip=QtNetwork.QHostAddress("192.168.1.229")
        # self.ip=QtNetwork.QHostAddress('172.20.10.4')
        self.TCP_LISTEN_TO_PORT=10000
        self.server=QtNetwork.QTcpServer()
        self.server.newConnection.connect(self.on_new_connection)
        self.sockets=[None,None]
        self.ips={}

    def on_new_connection(self):
        while self.server.hasPendingConnections():
            socket=self.server.nextPendingConnection()
            ip=socket.peerAddress().toString()
            if (id:=self.ips.get(ip))==None:
                id=len(self.ips)
                self.ips[ip]=id
            socket.setSocketOption(QtNetwork.QAbstractSocket.SocketOption.KeepAliveOption,1)
            if self.sockets[id]:
                self.sockets[id].abort()
            self.sockets[id]=socket
            socket.readyRead.connect(lambda: self.read(id))
            socket.disconnected.connect(lambda: self.on_disconnect(ip))
            print("Client Connected from IP %s"%ip)

    def on_disconnect(self,ip):
        print("Client Disconnected from IP",ip)

    def start_server(self):
        if self.server.listen(self.ip,self.TCP_LISTEN_TO_PORT):
            print("Server is listening on port: {}".format(self.TCP_LISTEN_TO_PORT))
        else:
            print("Server couldn't wake up")

    def read(self,id):
        self.sockets[id].readAll()
        self.hit.emit(id)

def main():
    app=QApplication(sys.argv)
    beep=QSoundEffect()
    beep.setSource(QUrl.fromLocalFile("beep.wav"))
    screen=app.primaryScreen()
    size=screen.size()
    window=ScoreboardWindow(size.width(),size.height(),beep)
    window.show()
    watchdog=SignalWatchdog()
    signal.signal(signal.SIGINT,lambda sig,_: app.quit())
    sys.exit(app.exec())

if __name__ == "__main__":
    main()