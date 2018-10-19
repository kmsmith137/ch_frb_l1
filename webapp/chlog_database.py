from __future__ import print_function
from datetime import datetime
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy import Column, Integer, String, DateTime

Base = declarative_base()
class LogMessage(Base):
    __tablename__ = 'log'

    id = Column(Integer, primary_key=True)
    host = Column(String)
    thread = Column(String)
    level = Column(String)
    filename = Column(String)
    linenum = Column(Integer)
    function = Column(String)
    date = Column(DateTime)
    msg = Column(String)

    def __repr__(self):
        return '<Log(host=%s, thread=%s, level=%s, where=%s:%i, func=[%s], date=%s: %s>' % (self.host, self.thread, self.level, self.filename, self.linenum, self.function, self.date, self.msg)


