// CConfig ��
//
// ���ڴ�������M$��ini��ʽ���ļ���
// ���������˿ɴ�����[section]�������ļ���
//

#ifndef __CCONFIG_H__
#define __CCONFIG_H__
#pragma once

#include <string>
#include <map>

// ö�������ֶεĻص�������
// ���� false ����ֹö�١�
typedef bool (* lpfnConfigEnum)(const char * section, const char * name, const char * value, void * param);

namespace Hippo {
class CConfig {
public:
        CConfig();
        ~CConfig();

        // ���������ļ���
        bool    Load(const char * filename);
        bool    LoadBuffer(const char * buffer);

        // ���������ļ���
        void    Save(const char * filename);

        // ���ֶΡ�[section] -> name
        std::string  Read(std::string section, std::string name);

        // д�ֶ�
        void    Write(std::string section, std::string name, std::string value);

        // ɾ���ֶ�
        void    Remove(std::string section, std::string name);

        // ����[section]���ֶΡ�
        std::string  Read(std::string name);

        // д��[section]���ֶΡ�
        void    Write(std::string name, std::string value);

        // ɾ����[section]���ֶΡ�
        void    Remove(std::string name);

        // ö�������ļ������е��ֶΡ�
        void    Enum(lpfnConfigEnum proc, void * param);

private:
        std::map<std::string, std::map<std::string, std::string> >       m_map;
};

}
#endif
