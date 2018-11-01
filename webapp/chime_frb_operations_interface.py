from datetime import datetime
import sqlalchemy
from sqlalchemy.orm import sessionmaker, scoped_session
from chime_frb_operations import UserAccounts, Acquisition, Base
from passlib.hash import sha256_crypt

def setup_connection(database):
    engine = sqlalchemy.create_engine(database)
    Base.metadata.create_all(engine)
    session_maker = scoped_session(sessionmaker(bind=engine))
    session = session_maker()
    return session

def read_from_db(database, model_type="acq"):
    assert model_type in ['acq', 'acc']
    db_session = setup_connection(database)
    if model_type == 'acq':
        db_info = db_session.query(Acquisition).all()
    else:
        db_info = db_session.query(UserAccounts).all()
    return db_info

def write_to_db(database, parts, model_type='acq'):
    assert model_type in ['acq', 'acc']
    db_session = setup_connection(database)
    if model_type == 'acq':
        acq = Acquisition(**parts)
        db_session.add(acq)
        db_session.commit()
    else:
        acc = UserAccounts(**parts)
        db_session.add(acc)
        db_session.commit()

def verify_password(password, confirm_password):
    if not (password==confirm_password):
        print ("Passwords did not match. Try again.")
        return False
    return True

def create_user_account(database='sqlite:///chime_frb_operations.sqlite3'):
    name = raw_input("Enter your name: ")
    username = raw_input("Enter your username: ")
    email = raw_input("Enter your email: ")
    verify=True
    while verify:
        password = raw_input("Enter your password: ")
        confirm_password = raw_input("Re-enter your password: ")
        if verify_password(password, confirm_password):
            verify=False

    user_parts = {
                  'name': name,
                  'username': username,
                  'password': sha256_crypt.hash(password),
                  'email': email,
                }

    write_to_db(database, user_parts, model_type="acc")

def remove_user_account(database='sqlite:///chime_frb_operations.sqlite3'):    
    session = setup_connection(database)
    name = raw_input("Enter your name: ")
    username = raw_input("Enter your username: ")
    email = raw_input("Enter your email: ")
    user = session.query(UserAccounts).filter(name==name, username==username, email==email).all()
    print("Removing: ", user[0])
    session.delete(user[0])
    session.commit()


def main():
    database = "sqlite:///chime_frb_operations.sqlite3"
    acq_info = read_from_db(database, model_type="acq")
    acc_info = read_from_db(database, model_type="acc")
    print acq_info
    print acc_info
 
if __name__=="__main__":
    main()
