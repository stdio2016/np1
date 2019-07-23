import os
from flask import Flask, render_template, session, request, redirect, url_for, send_from_directory
from flask_socketio import SocketIO, emit

app = Flask(__name__)

socketio = SocketIO(app)

clients = []

@app.route('/')
def index_page():
    return render_template('index.html')

@app.route('/js/<path:filename>')
def js_files(filename):
    return send_from_directory('js', filename)

@socketio.on('connect', namespace='/chat')
def hello():
    clients.append({
      'sid': request.sid,
      'name': 'anonymous',
      'ip': request.remote_addr,
      'port': request.environ['REMOTE_PORT']
    })
    emit('hello', {
      'name': 'anonymous',
      'ip': request.remote_addr,
      'port': request.environ['REMOTE_PORT']},
      broadcast=False)
    emit('join', None, broadcast=True, include_self=False)

@socketio.on('disconnect', namespace='/chat')
def test_disconnect():
    j = None
    for i in range(len(clients)):
        if clients[i]['sid'] == request.sid:
            j = i
            break
    if j is not None:
        print("someone left")
        emit('leave', {'name': clients[j]['name']}, broadcast=True)
        clients.pop(j)

def tellCommand(me, msg):
    if me['name'] == 'anonymous':
        emit('err', {'what': 'You are anonymous.'}, room=me['sid'])
        return

    arg1 = msg['args'].lstrip(' ')
    if arg1 == "":
        emit('err', {'what': "You didn't specify the receiver."}, room=me['sid'])
        return

    i = arg1.find(' ')
    if i == -1:
        emit('err', {'what': "Message is empty."}, room=me['sid'])
        return
    arg2 = arg1[i+1:]
    arg1 = arg1[0:i]
    if arg1 == 'anonymous':
        emit('err', {'what': 'The client to which you sent is anonymous.'}, room=me['sid'])
        return
    if arg2 == '':
        emit('err', {'what': "Message is empty."}, room=me['sid'])
        return

    j = None
    for i in range(len(clients)):
        if clients[i]['name'] == arg1:
            j = i
            break
    if j is None:
        emit('err', {'what': "The receiver doesn't exist."}, room=me['sid'])
    else:
        emit('success', {'what': "Your message has been sent."}, room=me['sid'])
        emit('tell', {'name': me['name'], 'msg': arg2}, room=clients[i]['sid'])

def is_ascii(s):
    for ch in s:
        if ord(ch) >= 128:
            return False
    return True

def nameCommand(me, msg):
    new_name = msg['args'].strip(' ')
    if new_name == '':
        emit('err', {'what': "You must specify a new name."}, room=me['sid'])
        return
    if not (2 <= len(new_name) <= 12 and is_ascii(new_name) and new_name.isalpha()):
        emit('err', {'what': "Username can only consists of 2~12 English letters."}, room=me['sid'])
        return
    if new_name == 'anonymous':
        emit('err', {'what': "Username cannot be anonymous."}, room=me['sid'])
        return
    collision = False
    for c in clients:
        if c is not me:
            if c['name'] == new_name:
                collision = True
                break
    if collision:
        emit('err', {'what': new_name+" has been used by others."}, room=me['sid'])
        return
    emit('name_me', {'now':new_name}, room=me['sid'])
    emit('name_other', {'old':me['name'], 'now':new_name}, broadcast=True, include_self=False)
    me['name'] = new_name

@socketio.on('msg', namespace='/chat')
def message(msg):
    j = None
    for i in range(len(clients)):
        if clients[i]['sid'] == request.sid:
            j = i
            break
    if j is not None:
        me = clients[j]
        msg['op'] = msg['op'].lower()
        if msg['op'] == 'yell':
            if msg['args'] == '':
                emit('err', {'what': "Message is empty."}, room=me['sid'])
                return
            emit('yell', {'name': me['name'], 'msg': msg['args']}, broadcast=True)
        elif msg['op'] == 'who':
            for c in clients:
                emit('who', {
                    'name': c['name'],
                    'ip': c['ip'], 'port': c['port'],
                    'me': c['sid'] == me['sid']
                }, room=clients[j]['sid'])
        elif msg['op'] == 'tell':
            tellCommand(me, msg)
        elif msg['op'] == 'name':
            nameCommand(me, msg)
        elif msg['op'] == 'exit':
            emit('err', {'what': 'exit command is not supported on Web platform'}, room=me['sid'])
        else:
            emit('err', {'what': 'Error command. The program supports name, who, yell, and tell command'}, room=me['sid'])

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=int(os.getenv('PORT', 8080)), debug=False)