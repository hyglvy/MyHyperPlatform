1. LogInitialization Log������ʼ�� - Ҫ�� BufferHead BufferTail���л�
2. GlobalVariablesInitialization �ܹ�������
3. PerfInitialization ��¼������ʼ�� (ȫ�ֺ����ĳ�ʼ��)
4. UtilInitialization ����ҳ�淶Χ(MmGetPhysicalMemoryRanges) EPTҳ�����ַ������ʼ�� RtlGetPcToFile ������ʼ��
5. PowerCallbackInitialization ע���Դ�ص�
6. HotplugCallbackInitialization ע���Ȳ�λص�
7. VmInitialization VM ��ʼ��ʼ��

#### VmInitialization
----
1. VmIsMyHyperPlatformIsInstalled   
   ����Ƿ��Ѿ���װ
2. VmIsVmxAvailable  
   ��鴦�����Ƿ�֧�� VMX �ܹ����Ƿ�֧�� EPT   
3. VmInitializeSharedData  
   ��ʼ���������ݶ�(����ʹ����;�ı�) I/O bitmap��MSR bitmap  
4. EptInitializeMtrrEntries  
   ���� MTRRs �ṹ - Ϊ�����Լ����� EPT ҳ���ʱ����׼����  
5. UtilForEachProcessor - VmStartVm - AsmInitializeVm(�Ĵ������棬 VM ����) - VmInitializeVm  
6. VmInitializeVm  
    6.1 EptInitialization  
        EPT ҳ��Ĺ���  
    6.2 ���� PROCESSOR_DATA ������ݽṹ VMCS VMXO  
    6.3 ���� VMX ģʽ - ע�� CR0��CR4 Ҫ��
    6.4 ��ʼ�� VMCS
    6.5 ��� VMCS �����ֶ�
    6.6 VmLaunchVm - ִ�� __vmx_vmlaunch 