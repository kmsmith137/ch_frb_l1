from __future__ import print_function
from datetime import datetime
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy import Column, Integer, String, DateTime

Base = declarative_base()
class UserAccounts(Base):
    __tablename__ = 'useraccounts'

    user_id = Column(Integer, primary_key=True)
    name = Column(String)
    username = Column(String)
    email = Column(String)
    password = Column(String)

    def __repr__(self):
        return '%s -- %s -- %s' % (self.name, self.username, self.email)

    def as_dict(self):
        return {
                'user_id': self.user_id,
                'name': self.name,
                'username': self.username,
                'email': self.email,
                'password': self.password
               }

class Acquisition(Base):
    __tablename__ = 'acquisition'

    acq_id = Column(Integer, primary_key=True)
    acquisition_name = Column(String)
    acquisition_device = Column(String)
    acquisition_start = Column(DateTime)
    beams = Column(String)
    frame0_ctime_us = Column(Integer)
    notes = Column(String)
    user = Column(String)

    def __repr__(self):
        return '%s -- %s -- %s -- %s -- %s -- %s --%s' % (self.acquisition_name, self.acquisition_device, self.acquisition_start, self.beams, self.frame0_ctime_us, self.notes, self.user)
    def as_dict(self):
        return {
                'acq_id': self.acq_id,
                'acquisition_name': self.acquisition_name,
                'acquisition_device': self.acquisition_device,
                'acquisition_start': self.acquisition_start,
                'beams': self.beams,
                'frame0_ctime_us': self.frame0_ctime_us,
                'notes': self.notes,
                'user': self.user,
               }

class SignUpForMonitoring(Base):
    __tablename__ = 'signupformonitoring'

    monitoring_id = Column(Integer, primary_key=True)
    name = Column(String)
    email = Column(String)
    monitoring_start = Column(String)
    monitoring_stop = Column(String)
    notes = Column(String)

    def __repr__(self):
        return '%s -- %s -- %s -- %s -- %s -- %s' % (self.name, self.monitoring_id, self.email, self.monitoring_start, self.monitoring_stop, self.notes)

    def as_dict(self):
        return {
                'monitoring_id': self.monitoring_id,
                'name': self.name,
                'email': self.email,
                'start': self.monitoring_start,
                'end': self.monitoring_stop,
                'notes': self.notes,
               }
