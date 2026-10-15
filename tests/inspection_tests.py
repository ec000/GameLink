import importlib.util,json,pathlib,unittest
from unittest.mock import patch
module=pathlib.Path(__file__).resolve().parents[1]/'scripts/inspect_environment.py'
spec=importlib.util.spec_from_file_location('inspection',module);inspection=importlib.util.module_from_spec(spec);spec.loader.exec_module(inspection)
class InspectionTests(unittest.TestCase):
 def test_status_secrets_not_retained(self):
  secret='NEVER-EXPORT-THIS-TOKEN'
  def fake(cmd):
   if cmd[-1]=='info':return {'status':'ok','stdout':json.dumps({'address':'aabbccddee','version':'1.16','online':True,'secret':secret,'config':{'secret':secret}})}
   if cmd[-1] in ('peers','listnetworks'):return {'status':'ok','stdout':'[]'}
   return {'status':'unavailable'}
  with patch.object(inspection,'run',fake):report=inspection.inspect('/nonexistent/gamelink-test')
  self.assertNotIn(secret,inspection.render(report));self.assertEqual(report['mutations'],[])
 def test_profiles_are_explicitly_unverified(self):
  root=pathlib.Path(__file__).resolve().parents[1]
  for path in (root/'profiles').glob('*.json'):
   p=json.loads(path.read_text());self.assertEqual(p['schema_version'],1);self.assertFalse(p['steam_api_interception'])
   self.assertEqual(p['validation']['status'],'unverified');self.assertEqual(p['ports'],[]);self.assertEqual(p['executables'],[])
if __name__=='__main__':unittest.main()
